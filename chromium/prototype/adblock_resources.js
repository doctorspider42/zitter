// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Zitter's own scriptlets and redirect resources (docs/ad-blocking.md), written
// from the public descriptions of uBlock Origin's scriptlet names and
// arguments, so `site##+js(set-constant, x, false)` works as people expect.
// No uBO or AdGuard code: theirs is GPL.
//
// adblock.rs splits this file at the `//@` lines:
//   //@ fn NAME                        a helper other resources depend on
//   //@ scriptlet NAME ALIAS... [needs DEP...]
//   //@ redirect NAME MIME ALIAS...    a `$redirect=` body (`base64` after MIME: binary)
// A scriptlet is one `function`; adblock-rust calls it with the rule's
// arguments as strings, after the helpers it needs, inside one closure.

//@ fn zitter-safe.fn
function zitterSafe() {
  // What the page cannot have wrapped yet (this runs before its scripts), and
  // a Function.prototype.toString that shows wrapped natives as native.
  if (zitterSafe.state) return zitterSafe.state;
  const masks = new WeakMap();
  const toString = Function.prototype.toString;
  const shim = new Proxy(toString, { apply: (target, self, args) => Reflect.apply(target, masks.get(self) ?? self, args) });
  masks.set(shim, toString);
  try { Object.defineProperty(Function.prototype, 'toString', { value: shim, writable: true, configurable: true }); } catch {}
  const mask = (wrapper, original) => { masks.set(wrapper, original); return wrapper; };
  // '' matches everything, '/re/flags' is a RegExp, '!x' negates, other text is literal.
  const pattern = raw => {
    let text = String(raw ?? ''), negate = false;
    if (text.startsWith('!')) { negate = true; text = text.slice(1); }
    const regex = /^\/(.+)\/([gimsuy]*)$/s.exec(text);
    const re = text === '' ? /^/ : regex ? new RegExp(regex[1], regex[2].replace('g', '')) : new RegExp(text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'));
    return { test: value => re.test(String(value)) !== negate };
  };
  // Scriptlet values, as uBO names them; anything else is refused.
  const value = raw => {
    const named = { undefined: undefined, false: false, true: true, null: null, '': '', emptyStr: '', noopFunc: function () {},
      trueFunc: function () { return true; }, falseFunc: function () { return false; }, emptyArr: [], emptyObj: {},
      noopPromiseResolve: function () { return Promise.resolve(); }, yes: 'yes', no: 'no' };
    if (Object.hasOwn(named, raw)) return { ok: true, value: named[raw] };
    if (/^-?\d{1,5}(\.\d+)?$/.test(raw)) return { ok: true, value: Number(raw) };
    return { ok: false };
  };
  // Calls `found(owner, leaf)` once every link of `chain` (a.b.c) exists, even
  // when the page creates them later.
  const onChain = (chain, found, owner = window) => {
    const dot = chain.indexOf('.');
    if (dot < 0) { found(owner, chain); return; }
    const head = chain.slice(0, dot), rest = chain.slice(dot + 1);
    const current = owner[head];
    if (current !== null && (typeof current === 'object' || typeof current === 'function')) { onChain(rest, found, current); return; }
    const descriptor = Object.getOwnPropertyDescriptor(owner, head);
    if (descriptor && !descriptor.configurable) return;
    let stored = current;
    Object.defineProperty(owner, head, { configurable: true, enumerable: true,
      get: () => stored,
      set: next => { stored = next; if (next !== null && (typeof next === 'object' || typeof next === 'function')) onChain(rest, found, next); } });
  };
  // Errors thrown on purpose (abort-*) never reach the page's error handlers.
  const magic = 'zitter' + Math.random().toString(36).slice(2);
  window.addEventListener('error', event => { if (String(event.message).includes(magic)) event.preventDefault(); }, true);
  const abort = () => { throw new ReferenceError(magic); };
  const ready = run => document.readyState === 'loading' ? document.addEventListener('DOMContentLoaded', run, { once: true }) : run();
  const observe = run => {
    run();
    ready(run);
    new MutationObserver(run).observe(document, { childList: true, subtree: true, attributes: true });
  };
  return (zitterSafe.state = { mask, pattern, value, onChain, abort, magic, ready, observe });
}

//@ scriptlet set-constant.js set-constant set needs zitter-safe.fn
function setConstant(chain = '', raw = '') {
  // Pins a global (or a path under one) to a harmless value, for detectors
  // that read `canRunAds` or `adsbygoogle.loaded`.
  const { value, onChain } = zitterSafe();
  const parsed = value(raw);
  if (!chain || !parsed.ok) return;
  onChain(chain, (owner, leaf) => {
    const descriptor = Object.getOwnPropertyDescriptor(owner, leaf);
    if (descriptor && !descriptor.configurable) { try { owner[leaf] = parsed.value; } catch {} return; }
    Object.defineProperty(owner, leaf, { configurable: false, enumerable: true, get: () => parsed.value, set: () => {} });
  });
}

//@ scriptlet abort-on-property-read.js abort-on-property-read aopr needs zitter-safe.fn
function abortOnPropertyRead(chain = '') {
  // A script stops with an error the moment it reads `chain`.
  const { onChain, abort } = zitterSafe();
  if (!chain) return;
  onChain(chain, (owner, leaf) => {
    Object.defineProperty(owner, leaf, { configurable: false, get: abort, set: () => {} });
  });
}

//@ scriptlet abort-on-property-write.js abort-on-property-write aopw needs zitter-safe.fn
function abortOnPropertyWrite(chain = '') {
  // A script stops with an error the moment it assigns `chain`.
  const { onChain, abort } = zitterSafe();
  if (!chain) return;
  onChain(chain, (owner, leaf) => {
    Object.defineProperty(owner, leaf, { configurable: false, get: () => undefined, set: abort });
  });
}

//@ scriptlet abort-current-script.js abort-current-script acs abort-current-inline-script acis needs zitter-safe.fn
function abortCurrentScript(chain = '', needle = '') {
  // The script now running stops when it touches `chain`, if its source (or
  // its URL) matches `needle`; other scripts keep the real value.
  const { onChain, pattern, magic } = zitterSafe();
  if (!chain) return;
  const match = pattern(needle);
  const guilty = () => {
    const script = document.currentScript;
    return script instanceof HTMLScriptElement && match.test(script.src || script.textContent);
  };
  onChain(chain, (owner, leaf) => {
    const descriptor = Object.getOwnPropertyDescriptor(owner, leaf);
    if (descriptor && !descriptor.configurable) return;
    let stored = descriptor?.get ? undefined : descriptor?.value;
    Object.defineProperty(owner, leaf, { configurable: false, enumerable: true,
      get() { if (guilty()) throw new ReferenceError(magic); return descriptor?.get ? descriptor.get.call(this) : stored; },
      set(next) { if (guilty()) throw new ReferenceError(magic); if (descriptor?.set) descriptor.set.call(this, next); else stored = next; } });
  });
}

//@ scriptlet prevent-setTimeout.js prevent-setTimeout no-setTimeout-if nostif setTimeout-defuser std needs zitter-safe.fn
function preventSetTimeout(needle = '', delay = '') {
  // Timers whose code matches `needle` (and whose delay matches `delay`) run
  // nothing: the "wall again in 3 s" timer.
  const { mask, pattern } = zitterSafe();
  const code = pattern(needle), wait = delay === '' ? null : pattern(delay.startsWith('/') || delay.startsWith('!') ? delay : `/^${delay}$/`);
  const original = window.setTimeout;
  window.setTimeout = mask(function (handler, ms, ...rest) {
    if (code.test(typeof handler === 'function' ? Function.prototype.toString.call(handler) : handler) && (!wait || wait.test(ms ?? 0)))
      handler = function () {};
    return Reflect.apply(original, this, [handler, ms, ...rest]);
  }, original);
}

//@ scriptlet prevent-setInterval.js prevent-setInterval no-setInterval-if nosiif setInterval-defuser sid needs zitter-safe.fn
function preventSetInterval(needle = '', delay = '') {
  const { mask, pattern } = zitterSafe();
  const code = pattern(needle), wait = delay === '' ? null : pattern(delay.startsWith('/') || delay.startsWith('!') ? delay : `/^${delay}$/`);
  const original = window.setInterval;
  window.setInterval = mask(function (handler, ms, ...rest) {
    if (code.test(typeof handler === 'function' ? Function.prototype.toString.call(handler) : handler) && (!wait || wait.test(ms ?? 0)))
      handler = function () {};
    return Reflect.apply(original, this, [handler, ms, ...rest]);
  }, original);
}

//@ scriptlet prevent-addEventListener.js prevent-addEventListener addEventListener-defuser aeld needs zitter-safe.fn
function preventAddEventListener(type = '', needle = '') {
  // Listeners of `type` whose code matches `needle` are never added: a
  // detector's load or error handler.
  const { mask, pattern } = zitterSafe();
  const kind = pattern(type), code = pattern(needle);
  const original = EventTarget.prototype.addEventListener;
  EventTarget.prototype.addEventListener = mask(function (event, listener, ...rest) {
    const text = typeof listener === 'function' ? Function.prototype.toString.call(listener) : String(listener?.handleEvent ?? listener);
    if (kind.test(event) && code.test(text)) return undefined;
    return Reflect.apply(original, this, [event, listener, ...rest]);
  }, original);
}

//@ scriptlet prevent-fetch.js prevent-fetch no-fetch-if needs zitter-safe.fn
function preventFetch(needle = '', body = '') {
  // Requests to a matching URL resolve with an empty 200 response instead of
  // going out or failing: bait requests look like they loaded.
  const { mask, pattern } = zitterSafe();
  const match = pattern(needle);
  const content = body === 'emptyObj' ? '{}' : body === 'emptyArr' ? '[]' : '';
  const original = window.fetch;
  window.fetch = mask(function (resource, ...rest) {
    const url = resource instanceof Request ? resource.url : String(resource);
    if (!match.test(url)) return Reflect.apply(original, this, [resource, ...rest]);
    const response = new Response(content, { status: 200, statusText: 'OK', headers: { 'Content-Type': content ? 'application/json' : 'text/plain' } });
    Object.defineProperty(response, 'url', { value: url });
    return Promise.resolve(response);
  }, original);
}

//@ scriptlet prevent-xhr.js prevent-xhr no-xhr-if needs zitter-safe.fn
function preventXhr(needle = '', body = '') {
  // The same for XMLHttpRequest: a matching request finishes at once with 200.
  const { mask, pattern } = zitterSafe();
  const match = pattern(needle);
  const content = body === 'emptyObj' ? '{}' : body === 'emptyArr' ? '[]' : '';
  const proto = XMLHttpRequest.prototype, open = proto.open, send = proto.send;
  const urls = new WeakMap();
  proto.open = mask(function (method, url, ...rest) { urls.set(this, String(url)); return Reflect.apply(open, this, [method, url, ...rest]); }, open);
  proto.send = mask(function (...args) {
    const url = urls.get(this);
    if (url === undefined || !match.test(url)) return Reflect.apply(send, this, args);
    const done = { readyState: 4, status: 200, statusText: 'OK', responseText: content, response: content, responseURL: url };
    for (const [key, value] of Object.entries(done)) Object.defineProperty(this, key, { value, configurable: true });
    setTimeout(() => { for (const type of ['readystatechange', 'load', 'loadend']) this.dispatchEvent(new Event(type)); }, 1);
  }, send);
}

//@ scriptlet json-prune.js json-prune needs zitter-safe.fn
function jsonPrune(remove = '', required = '') {
  // Strips ad fields from the site's own JSON (JSON.parse and Response.json):
  // paths separated by spaces, `[]` for every array item, `*` for any key.
  const { mask } = zitterSafe();
  const paths = remove.split(/\s+/).filter(Boolean), needs = required.split(/\s+/).filter(Boolean);
  if (!paths.length) return;
  const walk = (node, parts, visit) => {
    if (node === null || typeof node !== 'object') return;
    const [head, ...rest] = parts;
    const keys = head === '[]' ? (Array.isArray(node) ? node.keys() : []) : head === '*' ? Object.keys(node) : [head];
    for (const key of keys) {
      if (!rest.length) visit(node, key);
      else walk(node[key], rest, visit);
    }
  };
  const has = (node, path) => { let found = false; walk(node, path.split('.'), (owner, key) => { if (Object.hasOwn(owner, key)) found = true; }); return found; };
  const prune = data => {
    if (needs.length && !needs.every(path => has(data, path))) return data;
    for (const path of paths) walk(data, path.split('.'), (owner, key) => { delete owner[key]; });
    return data;
  };
  const parse = JSON.parse;
  JSON.parse = mask(function (...args) { return prune(Reflect.apply(parse, this, args)); }, parse);
  const json = Response.prototype.json;
  Response.prototype.json = mask(function (...args) { return Reflect.apply(json, this, args).then(prune); }, json);
}

//@ scriptlet remove-attr.js remove-attr ra needs zitter-safe.fn
function removeAttr(names = '', selector = '') {
  // Keeps removing attributes (`|`-separated) from matching elements as the page sets them.
  const { observe } = zitterSafe();
  const list = names.split('|').map(name => name.trim()).filter(Boolean);
  if (!list.length) return;
  const query = selector || list.map(name => `[${CSS.escape(name)}]`).join(',');
  observe(() => { try { for (const e of document.querySelectorAll(query)) for (const name of list) if (e.hasAttribute(name)) e.removeAttribute(name); } catch {} });
}

//@ scriptlet remove-class.js remove-class rc needs zitter-safe.fn
function removeClass(names = '', selector = '') {
  // The same for class names: the `no-scroll` an anti-adblock wall puts on <body>.
  const { observe } = zitterSafe();
  const list = names.split('|').map(name => name.trim()).filter(Boolean);
  if (!list.length) return;
  const query = selector || list.map(name => `.${CSS.escape(name)}`).join(',');
  observe(() => { try { for (const e of document.querySelectorAll(query)) for (const name of list) if (e.classList.contains(name)) e.classList.remove(name); } catch {} });
}

//@ scriptlet set-cookie.js set-cookie needs zitter-safe.fn
function setCookie(name = '', raw = '', path = '/') {
  // Sets the site's own "notice dismissed" cookie, for one year.
  if (!/^[\w.-]{1,64}$/.test(name) || !/^[\w.-]{0,64}$/.test(raw) || !/^\/[\w./-]*$/.test(path)) return;
  if (document.cookie.split('; ').some(entry => entry === `${name}=${raw}`)) return;
  document.cookie = `${name}=${raw}; path=${path}; max-age=31536000; SameSite=Lax`;
}

//@ scriptlet set-local-storage-item.js set-local-storage-item needs zitter-safe.fn
function setLocalStorageItem(key = '', raw = '') {
  // Sets (or with `$remove$`, removes) a localStorage item before the page reads it.
  if (!key || !/^[\w.:-]{0,128}$/.test(raw) && raw !== '$remove$') return;
  try { if (raw === '$remove$') localStorage.removeItem(key); else localStorage.setItem(key, raw); } catch {}
}

//@ scriptlet set-session-storage-item.js set-session-storage-item needs zitter-safe.fn
function setSessionStorageItem(key = '', raw = '') {
  if (!key || !/^[\w.:-]{0,128}$/.test(raw) && raw !== '$remove$') return;
  try { if (raw === '$remove$') sessionStorage.removeItem(key); else sessionStorage.setItem(key, raw); } catch {}
}

//@ scriptlet no-window-open-if.js no-window-open-if nowoif window.open-defuser needs zitter-safe.fn
function noWindowOpenIf(needle = '') {
  // Pop-unders to a matching URL never open.
  const { mask, pattern } = zitterSafe();
  const match = pattern(needle);
  const original = window.open;
  window.open = mask(function (url, ...rest) {
    if (match.test(url ?? '')) return null;
    return Reflect.apply(original, this, [url, ...rest]);
  }, original);
}

//@ scriptlet noeval-if.js noeval-if needs zitter-safe.fn
function noevalIf(needle = '') {
  // eval() of matching code does nothing.
  const { mask, pattern } = zitterSafe();
  const match = pattern(needle);
  const original = window.eval;
  window.eval = mask(function (code) { if (match.test(code)) return undefined; return original(code); }, original);
}

//@ scriptlet zitter-skip-draw.js zitter-skip-draw skip-draw needs zitter-safe.fn
function zitterSkipDraw(source = '', size = '') {
  // Zitter's own: ads painted into a canvas. Draws of an image whose URL
  // matches `source`, or whose natural size is `size` (WxH), paint nothing;
  // WebGL gets a transparent pixel instead. Sources are tracked through
  // <img>, <video>, ImageBitmap and blobs fetched by URL.
  const { mask, pattern } = zitterSafe();
  if (!source && !size) return;
  const match = source ? pattern(source) : null;
  const [w, h] = size ? size.split('x').map(Number) : [];
  const origins = new WeakMap();
  const url = item => item instanceof HTMLImageElement ? item.currentSrc || item.src : item instanceof HTMLVideoElement ? item.currentSrc || item.src
    : item instanceof SVGImageElement ? item.href?.baseVal : origins.get(item);
  const dims = item => item instanceof HTMLImageElement ? [item.naturalWidth, item.naturalHeight] : item instanceof HTMLVideoElement ? [item.videoWidth, item.videoHeight]
    : [item?.width, item?.height];
  const skip = item => {
    if (!item || typeof item !== 'object') return false;
    const from = url(item);
    if (match && from && match.test(from)) return true;
    if (size) { const [x, y] = dims(item); if (x === w && y === h) return true; }
    return false;
  };
  const blob = Response.prototype.blob;
  Response.prototype.blob = mask(function (...args) { const from = this.url; return Reflect.apply(blob, this, args).then(b => { origins.set(b, from); return b; }); }, blob);
  const bitmap = window.createImageBitmap;
  if (bitmap) window.createImageBitmap = mask(function (item, ...args) {
    const from = url(item);
    return Reflect.apply(bitmap, this, [item, ...args]).then(result => { if (from) origins.set(result, from); return result; });
  }, bitmap);
  for (const proto of [window.CanvasRenderingContext2D?.prototype, window.OffscreenCanvasRenderingContext2D?.prototype].filter(Boolean)) {
    const draw = proto.drawImage;
    proto.drawImage = mask(function (item, ...args) { if (skip(item)) return undefined; return Reflect.apply(draw, this, [item, ...args]); }, draw);
    const fill = proto.createPattern;
    proto.createPattern = mask(function (item, ...args) { if (skip(item)) return null; return Reflect.apply(fill, this, [item, ...args]); }, fill);
  }
  const blank = new ImageData(1, 1);
  for (const proto of [window.WebGLRenderingContext?.prototype, window.WebGL2RenderingContext?.prototype].filter(Boolean)) {
    for (const name of ['texImage2D', 'texSubImage2D']) {
      const upload = proto[name];
      proto[name] = mask(function (...args) {
        const last = args.length - 1;
        if (skip(args[last])) args[last] = blank;
        return Reflect.apply(upload, this, args);
      }, upload);
    }
  }
}

//@ redirect noop.js application/javascript noopjs noop-js
(function () {})();

//@ redirect noop.txt text/plain nooptext noop-txt

//@ redirect noop.json application/json noopjson noop-json
{}

//@ redirect noop.html text/html noopframe noop-html
<!doctype html>

//@ redirect noop.css text/css noopcss

//@ redirect 1x1.gif image/gif base64 1x1-transparent.gif 1x1-transparent-gif
R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAICRAEAOw==

//@ redirect 2x2.png image/png base64 2x2-transparent.png 2x2-transparent-png
iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAC0lEQVR4nGNgQAcAABIAAXfx+gAAAAAASUVORK5CYII=

//@ redirect google-analytics_analytics.js application/javascript google-analytics.com/analytics.js
(function () {
  // A stand-in for analytics.js: the queue runs, nothing is sent.
  const noop = function () {};
  const ga = function () { const last = arguments[arguments.length - 1]; if (last && typeof last.hitCallback === 'function') try { last.hitCallback(); } catch {} };
  ga.create = function () { return { get: noop, set: noop, send: noop }; };
  ga.getByName = function () { return null; };
  ga.getAll = function () { return []; };
  ga.remove = noop; ga.loaded = true;
  const name = window.GoogleAnalyticsObject || 'ga';
  const queue = window[name] && window[name].q;
  window[name] = ga;
  if (Array.isArray(queue)) for (const args of queue) try { ga.apply(null, args); } catch {}
})();

//@ redirect googletagservices_gpt.js application/javascript googletagservices.com/gpt.js google-publisher-tag gpt.js
(function () {
  // A stand-in for Google Publisher Tags: pages that wait for googletag to
  // render go on; slots exist and stay empty. Queued callbacks run.
  const noop = function () {};
  const self = function () { return this; };
  const slot = function (path, sizes, element) {
    const s = {};
    for (const name of ['addService', 'clearCategoryExclusions', 'clearTargeting', 'defineSizeMapping', 'set', 'setCategoryExclusion',
                        'setClickUrl', 'setCollapseEmptyDiv', 'setConfig', 'setForceSafeFrame', 'setSafeFrameConfig', 'setTargeting', 'updateTargetingFromMap'])
      s[name] = self.bind(s);
    s.getAdUnitPath = function () { return path || ''; };
    s.getSlotElementId = function () { return element || ''; };
    s.getTargeting = function () { return []; };
    s.getTargetingKeys = function () { return []; };
    s.getResponseInformation = function () { return null; };
    s.getSizes = function () { return sizes || []; };
    s.get = function () { return null; };
    return s;
  };
  const pubads = {};
  for (const name of ['addEventListener', 'removeEventListener', 'clear', 'clearCategoryExclusions', 'clearTagForChildDirectedTreatment',
                      'clearTargeting', 'collapseEmptyDivs', 'disableInitialLoad', 'display', 'enableAsyncRendering', 'enableLazyLoad',
                      'enableSingleRequest', 'enableVideoAds', 'refresh', 'set', 'setCategoryExclusion', 'setCentering', 'setCookieOptions',
                      'setForceSafeFrame', 'setLocation', 'setPrivacySettings', 'setPublisherProvidedId', 'setRequestNonPersonalizedAds',
                      'setSafeFrameConfig', 'setTagForChildDirectedTreatment', 'setTargeting', 'setVideoContent', 'updateCorrelator'])
    pubads[name] = self.bind(pubads);
  pubads.getSlots = function () { return []; };
  pubads.getTargeting = function () { return []; };
  pubads.getTargetingKeys = function () { return []; };
  pubads.isInitialLoadDisabled = function () { return false; };
  const companion = { setRefreshUnfilledSlots: noop, addEventListener: noop, enableSyncLoading: noop };
  const tag = window.googletag || {};
  const queue = Array.isArray(tag.cmd) ? tag.cmd : [];
  Object.assign(tag, {
    apiReady: true, pubadsReady: true, evalScripts: noop,
    pubads: function () { return pubads; }, companionAds: function () { return companion; }, content: function () { return { setContent: noop }; },
    defineSlot: slot, defineOutOfPageSlot: slot, destroySlots: noop, disablePublisherConsole: noop, display: noop, enableServices: noop,
    getVersion: function () { return '2026'; }, openConsole: noop, setAdIframeTitle: noop, setConfig: noop,
    sizeMapping: function () { const m = { addSize: function () { return m; }, build: function () { return []; } }; return m; },
    enums: { OutOfPageFormat: {}, TrafficSource: {} }, secureSignalProviders: { clearAllCache: noop, push: noop },
  });
  tag.cmd = { push: function (fn) { try { fn(); } catch {} return 1; } };
  window.googletag = tag;
  for (const fn of queue) try { fn(); } catch {}
})();

//@ redirect googlesyndication_adsbygoogle.js application/javascript googlesyndication.com/adsbygoogle.js googlesyndication-adsbygoogle
(function () {
  // A stand-in for adsbygoogle.js: pushes succeed and the slots look filled.
  const queue = window.adsbygoogle || [];
  window.adsbygoogle = { loaded: true, push: function () {} };
  for (const ins of document.querySelectorAll('ins.adsbygoogle')) ins.setAttribute('data-adsbygoogle-status', 'done');
  void queue;
})();

//@ scriptlet nano-setInterval-booster.js nano-setInterval-booster nano-sib needs zitter-safe.fn
function nanoSetIntervalBooster(needle = '', delay = '1000', boost = '0.05') {
  // Timers whose code matches run at `boost` times their delay: the countdown
  // before a download or a skip button.
  const { mask, pattern } = zitterSafe();
  const code = pattern(needle), wait = delay === '*' ? null : Number(delay || 1000), factor = Math.min(50, Math.max(0.001, Number(boost) || 0.05));
  const original = window.setInterval;
  window.setInterval = mask(function (handler, ms, ...rest) {
    if ((wait === null || ms === wait) && code.test(typeof handler === 'function' ? Function.prototype.toString.call(handler) : handler)) ms = ms * factor;
    return Reflect.apply(original, this, [handler, ms, ...rest]);
  }, original);
}

//@ scriptlet nano-setTimeout-booster.js nano-setTimeout-booster nano-stb needs zitter-safe.fn
function nanoSetTimeoutBooster(needle = '', delay = '1000', boost = '0.05') {
  const { mask, pattern } = zitterSafe();
  const code = pattern(needle), wait = delay === '*' ? null : Number(delay || 1000), factor = Math.min(50, Math.max(0.001, Number(boost) || 0.05));
  const original = window.setTimeout;
  window.setTimeout = mask(function (handler, ms, ...rest) {
    if ((wait === null || ms === wait) && code.test(typeof handler === 'function' ? Function.prototype.toString.call(handler) : handler)) ms = ms * factor;
    return Reflect.apply(original, this, [handler, ms, ...rest]);
  }, original);
}

//@ scriptlet nowebrtc.js nowebrtc needs zitter-safe.fn
function nowebrtc() {
  // RTCPeerConnection that never connects: ad scripts use it to find the local IP.
  const { mask } = zitterSafe();
  for (const name of ['RTCPeerConnection', 'webkitRTCPeerConnection']) {
    const original = window[name];
    if (typeof original !== 'function') continue;
    const fake = mask(function () {
      return { close() {}, createDataChannel() { return {}; }, createOffer() { return Promise.reject(new DOMException('', 'NotSupportedError')); },
               setLocalDescription() { return Promise.resolve(); }, setRemoteDescription() { return Promise.resolve(); },
               addEventListener() {}, removeEventListener() {} };
    }, original);
    fake.prototype = original.prototype;
    window[name] = fake;
  }
}

//@ scriptlet remove-node-text.js remove-node-text rmnt needs zitter-safe.fn
function removeNodeText(name = '', needle = '') {
  // Empties text nodes (or inline <script>s, with name "script") whose text
  // matches, as the parser adds them: an inline detector never runs.
  const { pattern } = zitterSafe();
  if (!name) return;
  const tag = name.toUpperCase(), match = pattern(needle);
  const check = node => {
    if (tag === '#TEXT' ? node.nodeType === 3 && match.test(node.nodeValue) : node.nodeName === tag && match.test(node.textContent)) node.textContent = '';
  };
  new MutationObserver(records => { for (const record of records) for (const node of record.addedNodes) check(node); })
    .observe(document, { childList: true, subtree: true });
}

//@ scriptlet abort-on-stack-trace.js abort-on-stack-trace aost needs zitter-safe.fn
function abortOnStackTrace(chain = '', needle = '') {
  // Touching `chain` throws when the call stack matches `needle`: a detector
  // that lives inside a bundle with the page's own code.
  const { onChain, pattern, magic } = zitterSafe();
  if (!chain) return;
  const match = pattern(needle);
  const guilty = () => match.test(String(new Error().stack));
  onChain(chain, (owner, leaf) => {
    const descriptor = Object.getOwnPropertyDescriptor(owner, leaf);
    if (descriptor && !descriptor.configurable) return;
    let stored = descriptor?.get ? undefined : descriptor?.value;
    Object.defineProperty(owner, leaf, { configurable: false, enumerable: true,
      get() { if (guilty()) throw new ReferenceError(magic); return descriptor?.get ? descriptor.get.call(this) : stored; },
      set(next) { if (guilty()) throw new ReferenceError(magic); if (descriptor?.set) descriptor.set.call(this, next); else stored = next; } });
  });
}

//@ scriptlet noeval.js noeval needs zitter-safe.fn
function noeval() {
  // eval() does nothing at all.
  const { mask } = zitterSafe();
  const original = window.eval;
  window.eval = mask(function () { return undefined; }, original);
}

//@ scriptlet no-requestAnimationFrame-if.js no-requestAnimationFrame-if norafif needs zitter-safe.fn
function noRequestAnimationFrameIf(needle = '') {
  const { mask, pattern } = zitterSafe();
  const code = pattern(needle);
  const original = window.requestAnimationFrame;
  window.requestAnimationFrame = mask(function (callback) {
    if (code.test(Function.prototype.toString.call(callback))) callback = function () {};
    return Reflect.apply(original, this, [callback]);
  }, original);
}

//@ scriptlet refresh-defuser.js refresh-defuser needs zitter-safe.fn
function refreshDefuser() {
  // <meta http-equiv="refresh"> that sends the page elsewhere is removed.
  const { observe } = zitterSafe();
  observe(() => { for (const meta of document.querySelectorAll('meta[http-equiv="refresh" i]')) meta.remove(); });
}

//@ scriptlet cookie-remover.js cookie-remover needs zitter-safe.fn
function cookieRemover(needle = '') {
  // Deletes the page's cookies whose name matches, now and as they come back.
  const { pattern } = zitterSafe();
  const match = pattern(needle);
  const sweep = () => {
    for (const entry of document.cookie.split(';')) {
      const name = entry.split('=')[0].trim();
      if (!name || !match.test(name)) continue;
      for (const domain of ['', location.hostname, '.' + location.hostname.split('.').slice(-2).join('.')])
        document.cookie = `${name}=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/${domain ? '; domain=' + domain : ''}`;
    }
  };
  sweep();
  window.addEventListener('beforeunload', sweep);
  setInterval(sweep, 2000);
}

//@ scriptlet json-prune-fetch-response.js json-prune-fetch-response jpfr needs zitter-safe.fn
function jsonPruneFetchResponse(remove = '', required = '', needle = '') {
  // json-prune, for the JSON bodies of fetch() responses whose URL matches.
  const { mask, pattern } = zitterSafe();
  const paths = remove.split(/\s+/).filter(Boolean), needs = required.split(/\s+/).filter(Boolean), url = pattern(needle);
  if (!paths.length) return;
  const walk = (node, parts, visit) => {
    if (node === null || typeof node !== 'object') return;
    const [head, ...rest] = parts;
    for (const key of head === '[]' ? (Array.isArray(node) ? [...node.keys()] : []) : head === '*' ? Object.keys(node) : [head])
      if (!rest.length) visit(node, key); else walk(node[key], rest, visit);
  };
  const has = (node, path) => { let found = false; walk(node, path.split('.'), (owner, key) => { if (Object.hasOwn(owner, key)) found = true; }); return found; };
  const fetch = window.fetch;
  window.fetch = mask(function (resource, ...rest) {
    const target = resource instanceof Request ? resource.url : String(resource);
    return Reflect.apply(fetch, this, [resource, ...rest]).then(response => {
      if (!url.test(target)) return response;
      return response.clone().json().then(data => {
        if (needs.length && !needs.every(path => has(data, path))) return response;
        for (const path of paths) walk(data, path.split('.'), (owner, key) => { delete owner[key]; });
        return new Response(JSON.stringify(data), { status: response.status, statusText: response.statusText, headers: response.headers });
      }).catch(() => response);
    });
  }, fetch);
}

//@ scriptlet popads-dummy.js popads-dummy popads.net needs zitter-safe.fn
function popadsDummy() {
  // PopAds' pop-under code finds its globals taken and gives up.
  for (const name of ['PopAds', 'popns']) {
    try { Object.defineProperty(window, name, { configurable: false, get: () => ({}), set: () => {} }); } catch {}
  }
}

//@ scriptlet nobab-scriptlet.js nobab bab-defuser nofab needs zitter-safe.fn
function noAdblockDetectors() {
  // BlockAdBlock, FuckAdBlock and their clones always report "no ad blocker".
  const fake = function () {};
  fake.prototype = { onDetected() { return this; }, onNotDetected(callback) { try { callback(); } catch {} return this; },
                     check() { return true; }, emitEvent() { return this; }, clearEvent() {}, setOption() { return this; } };
  for (const name of ['BlockAdBlock', 'FuckAdBlock', 'SniffAdBlock'])
    try { Object.defineProperty(window, name, { configurable: false, value: fake }); } catch {}
  for (const name of ['blockAdBlock', 'fuckAdBlock', 'sniffAdBlock'])
    try { Object.defineProperty(window, name, { configurable: false, value: new fake() }); } catch {}
}

//@ redirect nobab.js application/javascript blockadblock.js
(function () {
  // A stand-in for BlockAdBlock: it always finds no ad blocker.
  const fake = function () {};
  fake.prototype = { onDetected() { return this; }, onNotDetected(callback) { try { callback(); } catch (e) {} return this; },
                     check() { return true; }, emitEvent() { return this; }, clearEvent() {}, setOption() { return this; } };
  window.BlockAdBlock = fake;
  window.blockAdBlock = new fake();
})();

//@ redirect nofab.js application/javascript fuckadblock.js-3.2.0 fuckadblock.js
(function () {
  // A stand-in for FuckAdBlock: it always finds no ad blocker.
  const fake = function () {};
  fake.prototype = { onDetected() { return this; }, onNotDetected(callback) { try { callback(); } catch (e) {} return this; },
                     check() { return true; }, emitEvent() { return this; }, clearEvent() {}, setOption() { return this; } };
  window.FuckAdBlock = fake;
  window.fuckAdBlock = new fake();
})();

//@ redirect google-analytics_ga.js application/javascript google-analytics.com/ga.js
(function () {
  // A stand-in for the old ga.js: _gaq runs its callbacks, nothing is sent.
  const noop = function () {};
  const tracker = new Proxy({}, { get: function () { return noop; } });
  const gaq = { push: function () { for (const item of arguments) if (typeof item === 'function') try { item(); } catch (e) {} return 0; } };
  const queue = Array.isArray(window._gaq) ? window._gaq : [];
  window._gat = { _createTracker: function () { return tracker; }, _getTracker: function () { return tracker; },
                  _getTrackerByName: function () { return tracker; }, _anonymizeIp: noop };
  window._gaq = gaq;
  gaq.push.apply(gaq, queue);
})();

//@ redirect googletagmanager_gtm.js application/javascript googletagmanager.com/gtm.js
(function () {
  // A stand-in for Tag Manager and gtag.js: dataLayer events that wait for
  // the container run their callbacks; nothing is sent.
  const layer = window.dataLayer = window.dataLayer || [];
  const done = function (item) { if (item && typeof item.eventCallback === 'function') setTimeout(function () { try { item.eventCallback(); } catch (e) {} }, 1); };
  layer.forEach(done);
  const push = layer.push.bind(layer);
  layer.push = function () { for (const item of arguments) done(item); return push.apply(null, arguments); };
  window.google_tag_manager = window.google_tag_manager || {};
})();

//@ redirect amazon_apstag.js application/javascript amazon-adsystem.com/aax2/apstag.js
(function () {
  // A stand-in for Amazon's apstag: bids come back empty at once.
  const noop = function () {};
  const queue = window.apstag && Array.isArray(window.apstag._Q) ? window.apstag._Q : [];
  window.apstag = {
    _Q: [], init: noop, setDisplayBids: noop, targetingKeys: function () { return []; }, deleteId: noop, rpa: noop, upa: noop, dpa: noop,
    fetchBids: function (config, callback) { if (typeof callback === 'function') setTimeout(function () { try { callback([]); } catch (e) {} }, 1); },
  };
  for (const entry of queue) try { window.apstag[entry[0]] && window.apstag[entry[0]].apply(null, entry[1]); } catch (e) {}
})();
