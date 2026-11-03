// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
import { lookup } from 'node:dns/promises';
import { isIP } from 'node:net';

// No browser cookies, proxies, credentials or profile network context are used.
export function publicAddress(address) {
  if (address.includes(':')) {
    const a = address.toLowerCase();
    // Only global unicast IPv6; mapped IPv4 and transition ranges are denied.
    return /^[23]/.test(a) && !a.startsWith('2001:') && !a.startsWith('2002:');
  }
  const p = address.split('.').map(Number);
  return p.length === 4 && p.every(n => Number.isInteger(n) && n >= 0 && n <= 255) &&
    ![0, 10, 127].includes(p[0]) && p[0] < 224 &&
    !(p[0] === 169 && p[1] === 254) && !(p[0] === 172 && p[1] >= 16 && p[1] <= 31) &&
    !(p[0] === 192 && [0, 168].includes(p[1])) && !(p[0] === 100 && p[1] >= 64 && p[1] <= 127) &&
    !(p[0] === 198 && [18, 19].includes(p[1]));
}

export async function publicURL(raw) {
  const url = new URL(raw);
  if (url.protocol !== 'https:' || url.username || url.password || (url.port && url.port !== '443'))
    throw new Error('Research only reads public HTTPS pages.');
  const host = url.hostname.replace(/^\[|\]$/g, '');
  const addresses = isIP(host) ? [{ address: host }] : await lookup(host, { all: true });
  if (!addresses.length || addresses.some(a => !publicAddress(a.address))) throw new Error('Private network destinations are blocked.');
  url.hash = '';
  return url;
}

export function plain(html) {
  return html.replace(/<(script|style|noscript|svg|form)\b[^>]*>[\s\S]*?<\/\1\s*>/gi, ' ')
    .replace(/<[^>]*>/g, ' ').replace(/&nbsp;/g, ' ').replace(/&amp;/g, '&')
    .replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&#39;/g, "'").replace(/&quot;/g, '"')
    .replace(/\s+/g, ' ').trim();
}

export function extractLinks(html, base) {
  const links = [], seen = new Set();
  for (const match of html.matchAll(/<a\b[^>]*href\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))[^>]*>([\s\S]*?)<\/a>/gi)) {
    try {
      let target = new URL((match[1] ?? match[2] ?? match[3]).replace(/&amp;/g, '&'), base);
      if ((target.hostname === 'duckduckgo.com' || target.hostname.endsWith('.duckduckgo.com')) && target.searchParams.has('uddg'))
        target = new URL(target.searchParams.get('uddg'));
      target.hash = '';
      if (target.protocol === 'https:' && !target.username && !target.password && !seen.has(target.href)) {
        seen.add(target.href); links.push({ title: plain(match[4]).slice(0, 180), url: target.href });
      }
    } catch {}
    if (links.length >= 80) break;
  }
  return links;
}

export class Web {
  constructor({ fetcher = fetch, ttl = 300000, limit = 128 } = {}) {
    this.fetcher = fetcher; this.ttl = ttl; this.limit = limit; this.cache = new Map();
  }
  async cached(key, work, signal) {
    const old = this.cache.get(key);
    if (old && old.until > Date.now()) return old.value;
    // In-flight work is shared, including across parallel research workers.
    const value = work();
    this.cache.set(key, { until: Date.now() + this.ttl, value });
    while (this.cache.size > this.limit) this.cache.delete(this.cache.keys().next().value);
    try { return await value; } catch (e) { this.cache.delete(key); throw e; }
  }
  async read(raw, signal) {
    let url = await publicURL(raw);
    return this.cached(url.href, async () => {
      // Pin DNS resolution on every connection to prevent DNS rebinding.
      const { request, Agent } = await import('node:https');
      for (let hop = 0; hop < 5; hop++) {
        const response = await new Promise((resolve, reject) => {
          const req = request(url, { signal: AbortSignal.any([signal, AbortSignal.timeout(20000)]),
            agent: new Agent({ lookup(host, options, callback) {
              lookup(host, { all: true }).then(records => {
                if (!records.length || records.some(r => !publicAddress(r.address))) return callback(new Error('Private address blocked'));
                callback(null, options.all ? records : records[0].address, records[0].family);
              }, callback);
            } }), headers: { 'User-Agent': 'ZitterResearch/1.0', Accept: 'text/html,text/plain,application/json' },
          }, resolve);
          req.on('error', reject); req.end();
        });
        if ([301, 302, 303, 307, 308].includes(response.statusCode)) {
          response.resume(); url = await publicURL(new URL(response.headers.location, url).href); continue;
        }
        if (response.statusCode !== 200) { response.resume(); throw new Error(`Page returned ${response.statusCode}`); }
        if (!/text\/(html|plain)|application\/(json|xhtml\+xml)/i.test(response.headers['content-type'] ?? '')) {
          response.destroy(); throw new Error('Unsupported page type');
        }
        let bytes = 0, body = '';
        const decoder = new TextDecoder('utf-8');
        for await (const chunk of response) {
          bytes += chunk.length;
          if (bytes > 2 * 1024 * 1024) { response.destroy(); throw new Error('Page exceeds research limit'); }
          body += decoder.decode(chunk, { stream: true });
        }
        body += decoder.decode();
        const links = extractLinks(body, url);
        return { url: url.href, text: plain(body).slice(0, 24000), links, fetched_at: new Date().toISOString() };
      }
      throw new Error('Too many redirects');
    }, signal);
  }
  async search(query, signal) {
    if (typeof query !== 'string' || !query.trim() || query.length > 600) throw new Error('Invalid search query');
    return this.cached(`search:${query}`, async () => {
      // Brave is optional; DuckDuckGo's public HTML endpoint works without a key.
      if (process.env.BRAVE_SEARCH_API_KEY) {
        const response = await this.fetcher(`https://api.search.brave.com/res/v1/web/search?q=${encodeURIComponent(query)}&count=10`, {
          headers: { 'X-Subscription-Token': process.env.BRAVE_SEARCH_API_KEY }, signal,
        });
        if (!response.ok) throw new Error(`Search failed (${response.status})`);
        const data = await response.json();
        return (data.web?.results ?? []).slice(0, 10).map(r => ({ title: r.title, url: r.url, snippet: plain(r.description ?? '') }));
      }
      const result = await this.read(`https://html.duckduckgo.com/html/?q=${encodeURIComponent(query)}`, signal);
      // Fetch the HTML separately only through the same guarded reader, which retains links below.
      const results = result.links.filter(link => {
        const host = new URL(link.url).hostname;
        return link.title && host !== 'duckduckgo.com' && !host.endsWith('.duckduckgo.com');
      }).slice(0, 10);
      return { search_url: result.url, text: result.text, results };
    }, signal);
  }
}
