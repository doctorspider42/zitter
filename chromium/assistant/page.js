// Executed only in Zitter's private isolated world. No page script can access the map.
(() => {
  const args = ZITTER_ARGS;
  const sensitive = e => !!e.closest('[contenteditable], [data-private], [data-sensitive]') ||
    /password|hidden|file/i.test(e.type ?? '') ||
    /password|passwd|secret|token|credit|card.?number|cvv|cvc|otp|one.?time|cc-/i.test(
      [e.name, e.id, e.autocomplete, e.getAttribute('aria-label')].join(' '));
  const visible = e => {
    const r = e.getBoundingClientRect(), s = getComputedStyle(e);
    if (r.width <= 0 || r.height <= 0 || s.visibility === 'hidden' || s.display === 'none') return false;
    for (let p = e; p; p = p.parentElement) {
      const style = getComputedStyle(p);
      if (Number(style.opacity) === 0 || style.contentVisibility === 'hidden' || p.hidden) return false;
    }
    return true;
  };
  const label = e => e.getAttribute('aria-label') || e.placeholder ||
    (e.matches('input,textarea,select') ? e.labels?.[0]?.textContent || '' : e.textContent || '');
  if (args.tool === 'read_page') {
    const map = new Map(), elements = [];
    // getRandomValues also works on ordinary HTTP pages; randomUUID requires HTTPS.
    const snapshot = Array.from(crypto.getRandomValues(new Uint32Array(4)), n => n.toString(16)).join('-');
    for (const e of document.querySelectorAll('a[href],button,input,textarea,select,[role="button"]')) {
      if (elements.length >= 160) break;
      if (!visible(e) || sensitive(e)) continue;
      const id = `${snapshot}:${elements.length}`;
      map.set(id, e);
      elements.push({ id, tag: e.tagName, label: label(e).slice(0, 160),
        type: e.type ?? '', confirmation: e.matches('button,input[type=submit]') || e.getAttribute('role') === 'button' });
    }
    globalThis.__zitterAssistant = { map, url: location.href };
    const walker = document.createTreeWalker(document.body || document.documentElement, NodeFilter.SHOW_TEXT);
    let text = '', node;
    while ((node = walker.nextNode()) && text.length < 24000) {
      const p = node.parentElement;
      if (!p || p.closest('script,style,noscript,input,textarea,select,[contenteditable],[data-private],[data-sensitive]') ||
          sensitive(p) || !visible(p)) continue;
      text += node.textContent.replace(/\s+/g, ' ') + ' ';
    }
    return { text: text.slice(0, 24000), elements };
  }
  if (args.tool === 'scroll') { scrollBy(0, Math.max(-3000, Math.min(3000, Number(args.dy) || 0))); return { ok: true }; }
  const state = globalThis.__zitterAssistant;
  const e = state?.url === location.href ? state.map.get(args.element) : null;
  if (!e?.isConnected || !visible(e) || sensitive(e)) return { error: 'Element expired, hidden or protected. Read the page again.' };
  if (args.tool === 'rect') {
    // Natively requested for the consent bubble only; brings the element into view.
    e.scrollIntoView({ block: 'center', inline: 'nearest' });
    const r = e.getBoundingClientRect();
    return { x: r.x, y: r.y, width: r.width, height: r.height };
  }
  if (args.tool === 'click') {
    if (!args.confirmed)
      return { confirmation_required: true, label: (label(e) || 'Click').slice(0, 120) };
    if (e.tagName === 'A' && !/^https?:$/.test(new URL(e.href, location.href).protocol)) return { error: 'Only HTTP(S) links are allowed' };
    e.click(); return { ok: true };
  }
  if (args.tool === 'type') {
    if (!e.matches('input,textarea') || e.disabled || e.readOnly || typeof args.text !== 'string' || args.text.length > 16000)
      return { error: 'Not an editable, non-sensitive field' };
    const prototype = e.tagName === 'TEXTAREA' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
    Object.getOwnPropertyDescriptor(prototype, 'value').set.call(e, args.text);
    e.dispatchEvent(new Event('input', { bubbles: true })); e.dispatchEvent(new Event('change', { bubbles: true }));
    return { ok: true };
  }
  return { error: 'Unknown page operation' };
})()
