import test from 'node:test';
import assert from 'node:assert/strict';
import { Engine, parallel } from './engine.mjs';
import { decode } from './providers.mjs';
import { publicAddress, publicURL, plain, extractLinks, Web } from './web.mjs';
import { server } from './server.mjs';

const done = answer => ({ answer, actions: [] });
const action = (tool, args = {}) => ({ tool, args });
const request = (extra = {}) => ({ id: 'session', config: { provider: 'fake', model: 'test' },
  permissions: { web: true, tabs: false, pages: false, actions: false, history: false },
  prompt: 'Help me', context: {}, date: '2026-11-03', ...extra });

test('provider response validation rejects malformed action arguments', () => {
  assert.deepEqual(decode('{"answer":"ok","actions":[{"tool":"list_tabs","args":"{}"}]}'),
    { answer: 'ok', actions: [action('list_tabs')] });
  for (const raw of ['null', '{}', '{"answer":1,"actions":[]}', '{"answer":"","actions":[{"tool":"click","args":"null"}]}'])
    assert.throws(() => decode(raw));
});

test('browser actions leave the runtime and resume only with native results', async () => {
  const calls = [];
  const engine = new Engine({ providerFactory: () => ({ async complete(system, messages) {
    calls.push(structuredClone(messages));
    return calls.length === 1 ? { answer: 'Reading', actions: [action('read_page', { tab: '@current' })] } : done('Read successfully');
  } }) });
  assert.equal((await engine.turn(request(), new AbortController().signal)).actions[0].tool, 'read_page');
  assert.equal((await engine.resume('session', [{ result: 'text' }], new AbortController().signal)).answer, 'Read successfully');
  assert.match(calls[1].at(-1).content, /untrusted_tool_results/);
});

test('disabled web permission prevents network reads even when model requests them', async () => {
  let calls = 0, reads = 0;
  const engine = new Engine({ providerFactory: () => ({ async complete() {
    return ++calls === 1 ? { answer: '', actions: [action('web_read', { url: 'https://example.com' })] } : done('Permission missing');
  } }), web: { async read() { reads++; } } });
  await engine.turn(request({ permissions: { web: false } }), new AbortController().signal);
  assert.equal(reads, 0);
});

test('model and permissions cannot be silently changed within an existing session', async () => {
  const engine = new Engine({ providerFactory: () => ({ complete: async () => done('Hi') }) });
  const signal = new AbortController().signal;
  await engine.turn(request(), signal);
  await assert.rejects(engine.turn(request({ permissions: { history: true } }), signal), /new session/);
  await assert.rejects(engine.turn(request({ config: { model: 'other' } }), signal), /new session/);
});

test('parallel workers preserve order and enforce concurrency', async () => {
  let active = 0, max = 0;
  const result = await parallel([1, 2, 3, 4, 5, 6], 2, async n => {
    active++; max = Math.max(max, active);
    await new Promise(resolve => setTimeout(resolve, 5)); active--; return n * 2;
  });
  assert.equal(max, 2); assert.deepEqual(result, [2, 4, 6, 8, 10, 12]);
});

test('research shares a global model concurrency bound and exact request cache', async () => {
  let active = 0, maximum = 0, calls = 0;
  const engine = new Engine({ concurrency: 2 });
  const session = { config: { provider: 'fake', model: 'one' }, provider: { async complete() {
    calls++; active++; maximum = Math.max(maximum, active);
    await new Promise(r => setTimeout(r, 3)); active--; return done('cached');
  } } };
  const signal = new AbortController().signal;
  await Promise.all(Array.from({ length: 12 }, (_, i) => engine.complete(session, [{ role: 'user', content: String(i) }], signal)));
  assert.equal(maximum, 2); assert.equal(calls, 12);
  await engine.complete(session, [{ role: 'user', content: '0' }], signal);
  assert.equal(calls, 12);
});

test('deep research plans angles, denies native tools to workers and synthesizes evidence', async () => {
  let reads = 0, synthesis = false;
  const engine = new Engine({ providerFactory: () => ({ async complete(system, messages) {
    const text = messages.at(-1).content;
    if (text.startsWith('Plan deep research')) return { answer: '', actions: [action('web_search', { query: 'reviews' }), action('web_search', { query: 'discussions' })] };
    if (text.includes('untrusted_research_reports')) { synthesis = true; return done('Report [source](https://example.com)'); }
    if (messages.length === 2) return { answer: '', actions: [action('web_read', { url: 'https://example.com' }), action('history', { query: 'secret' })] };
    assert.match(text, /cannot access browser/);
    return done('Evidence from https://example.com');
  } }), web: { async search() { return []; }, async read() { reads++; return { url: 'https://example.com', text: 'Evidence' }; } } });
  const result = await engine.turn(request({ research: true }), new AbortController().signal);
  assert.equal(reads, 2); assert.equal(synthesis, true); assert.match(result.answer, /Report/); assert.deepEqual(result.actions, []);
});

test('background turns tell the model and research honors the session concurrency setting', async () => {
  let first, active = 0, maximum = 0;
  const engine = new Engine({ providerFactory: () => ({ async complete(system, messages) {
    const text = messages.at(-1).content;
    first ??= messages[0].content;
    if (text.startsWith('Plan deep research')) return { answer: '', actions: ['a', 'b', 'c', 'd'].map(query => action('web_search', { query })) };
    if (text.includes('untrusted_research_reports')) return done('Report');
    return done('Angle');
  } }), web: { async search() {
    active++; maximum = Math.max(maximum, active); await new Promise(r => setTimeout(r, 5)); active--; return [];
  }, async read() { return {}; } } });
  await engine.turn(request({ research: true, background: true, concurrency: 2 }), new AbortController().signal);
  assert.equal(JSON.parse(first).background, true);
  assert.equal(maximum, 2);
});

test('research blocks private, credentialed, non-HTTPS and mapped IPv6 URLs', async () => {
  for (const a of ['127.0.0.1', '10.0.0.1', '172.31.0.2', '192.168.1.1', '169.254.169.254', '100.64.0.1', '::1', '::ffff:127.0.0.1', 'fc00::1', '2002:7f00:1::'])
    assert.equal(publicAddress(a), false, a);
  assert.equal(publicAddress('8.8.8.8'), true);
  for (const u of ['http://example.com', 'https://user:pass@example.com', 'https://127.0.0.1', 'https://[::1]', 'https://example.com:444'])
    await assert.rejects(publicURL(u));
});

test('page reader strips forms, scripts and style before model context', () => {
  assert.equal(plain('<p>hello</p><script>secret</script><form>password</form><style>hide</style><b>world</b>'), 'hello world');
});

test('page links support HTML attribute forms, deduplicate and reject credentialed URLs', () => {
  const links = extractLinks(`<a href=https://example.com/page>Unquoted</a><a href="https://example.com/page#part">Duplicate</a>
    <a href='https://other.example/'>Other</a><a href="javascript:alert(1)">Script</a>
    <a href="https://user:secret@example.com">Secret</a>
    <a href="https://duckduckgo.com/l/?uddg=https%3A%2F%2Fsource.example%2F">Source</a>`, 'https://base.example');
  assert.deepEqual(links.map(l => l.url), ['https://example.com/page', 'https://other.example/', 'https://source.example/']);
});

test('cache coalesces in-flight reads and evicts failures', async () => {
  const web = new Web(); let calls = 0;
  const work = async () => { calls++; await new Promise(r => setTimeout(r, 5)); return 'same'; };
  assert.deepEqual(await Promise.all([web.cached('a', work), web.cached('a', work)]), ['same', 'same']);
  assert.equal(calls, 1);
  await assert.rejects(web.cached('b', async () => { throw new Error('fail'); }));
  assert.equal(await web.cached('b', async () => 'retry'), 'retry');
});

test('loopback RPC rejects origins and invalid tokens, never echoes arbitrary provider errors', async t => {
  const app = server('secret', { turn: async () => { throw new Error('model request body password abc'); } });
  await new Promise(r => app.listen(0, '127.0.0.1', r));
  t.after(() => new Promise(r => app.close(r)));
  const url = `http://127.0.0.1:${app.address().port}`;
  assert.equal((await fetch(url + '/health')).status, 403);
  assert.equal((await fetch(url + '/health', { headers: { Authorization: 'Bearer secret', Origin: 'https://evil.test' } })).status, 403);
  assert.deepEqual(await (await fetch(url + '/health', { headers: { Authorization: 'Bearer secret' } })).json(), { ready: true });
  const error = await (await fetch(url + '/turn', { method: 'POST', headers: { Authorization: 'Bearer secret' }, body: JSON.stringify({ id: 'a' }) })).json();
  assert.doesNotMatch(error.error, /password|abc/);
});

test('screenshots become images, and only the newest stay in the transcript', async () => {
  const calls = [];
  const engine = new Engine({ providerFactory: () => ({ async complete(system, messages) {
    calls.push(structuredClone(messages));
    return calls.length < 3 ? { answer: '', actions: [action('screenshot', { tab: '@current' })] } : done('Seen');
  } }) });
  const signal = new AbortController().signal;
  await engine.turn(request({ id: 'shots' }), signal);
  await engine.resume('shots', [{ tool: 'screenshot', result: { image: 'Zmlyc3Q=', width: 10, height: 10 } }], signal);
  assert.deepEqual(calls[1].at(-1).images, ['Zmlyc3Q=']);
  assert.doesNotMatch(calls[1].at(-1).content, /Zmlyc3Q=/);
  await engine.resume('shots', [{ tool: 'screenshot', result: { image: 'c2Vjb25k' } }], signal);
  assert.deepEqual(calls[2].at(-1).images, ['c2Vjb25k']);
  assert.equal(calls[2].filter(m => m.images).length, 1);
});

test('a saved conversation continues with its earlier messages, and old runtimes make room', async () => {
  const calls = [];
  const engine = new Engine({ providerFactory: () => ({ async complete(system, messages) { calls.push(structuredClone(messages)); return done('Again'); } }) });
  const signal = new AbortController().signal;
  await engine.turn(request({ id: 'restored', history: [{ role: 'user', text: 'Which books?' }, { role: 'assistant', text: 'Solaris and Eden.' }] }), signal);
  assert.match(calls[0][0].content, /Which books/);
  assert.equal(JSON.parse(calls[0][1].content).answer, 'Solaris and Eden.');
  assert.match(calls[0][2].content, /Help me/);
  for (let i = 0; i < 40; i++) await engine.turn(request({ id: `s${i}` }), signal);
  assert.equal(engine.sessions.size, 32);
});

test('the answer being written is visible through progress until the turn returns', async () => {
  let release, seen;
  const engine = new Engine({ providerFactory: () => ({ async complete(system, messages, signal, onPartial) {
    onPartial('Pisz'); await new Promise(r => { release = r; }); return done('Piszę dalej');
  } }) });
  const turn = engine.turn(request({ id: 'live' }), new AbortController().signal);
  await new Promise(r => setImmediate(r));
  seen = engine.sessions.get('live').partial;
  release();
  assert.equal(seen, 'Pisz');
  assert.equal((await turn).answer, 'Piszę dalej');
  assert.equal(engine.sessions.get('live').partial, '');
});
