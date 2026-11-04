// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
import { createServer } from 'node:http';
import { timingSafeEqual } from 'node:crypto';
import { Engine } from './engine.mjs';
import { pathToFileURL } from 'node:url';
import { AssistantError } from './errors.mjs';

export function server(token, engine = new Engine()) {
  const running = new Map();
  return createServer(async (req, res) => {
    res.setHeader('Content-Type', 'application/json');
    res.setHeader('Cache-Control', 'no-store');
    const supplied = Buffer.from(req.headers.authorization ?? '');
    const expected = Buffer.from(`Bearer ${token}`);
    if (req.headers.origin || supplied.length !== expected.length || !timingSafeEqual(supplied, expected)) {
      res.writeHead(403); res.end('{"error":"Forbidden"}'); return;
    }
    if (req.method === 'GET' && req.url === '/health') { res.end('{"ready":true}'); return; }
    if (req.method !== 'POST') { res.writeHead(405); res.end('{}'); return; }
    // Screenshots make tool results large: up to 16 MB.
    const chunks = [];
    let size = 0;
    try {
      for await (const chunk of req) {
        size += chunk.length; chunks.push(chunk);
        if (size > 16 * 1024 * 1024) throw new Error('Request too large');
      }
      const value = JSON.parse(Buffer.concat(chunks).toString('utf8'));
      if (typeof value.id !== 'string' || value.id.length > 100) throw new Error('Invalid session');
      if (req.url === '/cancel' || req.url === '/delete') {
        running.get(value.id)?.abort(); running.delete(value.id);
        if (req.url === '/delete') engine.sessions.delete(value.id);
        res.end('{}'); return;
      }
      if (!['/turn', '/continue'].includes(req.url)) { res.writeHead(404); res.end('{}'); return; }
      if (running.has(value.id)) throw new Error('Session is already running');
      const abort = new AbortController(); running.set(value.id, abort);
      const timer = setTimeout(() => abort.abort(), 600000);
      res.on('close', () => { if (!res.writableEnded) abort.abort(); });
      try {
        const result = req.url === '/turn' ? await engine.turn(value, abort.signal) : await engine.resume(value.id, value.results, abort.signal);
        res.end(JSON.stringify(result));
      } finally { clearTimeout(timer); if (running.get(value.id) === abort) running.delete(value.id); }
    } catch (e) {
      // Provider exceptions can include prompt data; only expose known operational errors.
      const message = e instanceof AssistantError ? e.message.slice(0, 240)
        : 'Assistant request failed. Check the provider setup or try again.';
      res.writeHead(400); res.end(JSON.stringify({ error: message }));
    }
  });
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const port = Number(process.argv[2]), token = process.argv[3];
  if (!Number.isInteger(port) || port < 1024 || port > 65535 || !/^[a-f0-9]{32,64}$/.test(token ?? '')) process.exit(2);
  const app = server(token);
  app.on('error', () => process.exit(1));
  app.listen(port, '127.0.0.1');
  // Parent death also ends the service; do not keep a browser assistant orphan alive.
  const parent = process.ppid;
  setInterval(() => { try { process.kill(parent, 0); } catch { process.exit(0); } }, 2000).unref();
}
