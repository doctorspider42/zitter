import test from 'node:test';
import assert from 'node:assert/strict';
import { access } from 'node:fs/promises';
import { Provider } from './providers.mjs';
import { createServer } from 'node:http';
import { Engine } from './engine.mjs';

const response = { answer: 'Ready', actions: [{ tool: 'browser_help', args: '{}' }] };
const signal = new AbortController().signal;

test('model IDs cannot be interpreted as CLI options', () => {
  for (const model of ['--dangerously-skip-permissions', '-p', 'model\n--flag', ''])
    assert.throws(() => new Provider({ provider: 'claude', model }), /valid model/);
});

test('Ollama transport completes a real HTTP browser tool round trip', async t => {
  let turns = 0;
  const ollama = createServer(async (req, res) => {
    assert.equal(req.url, '/api/chat'); let body = '';
    for await (const chunk of req) body += chunk;
    const request = JSON.parse(body); assert.equal(request.model, 'test-local');
    const output = ++turns === 1 ? { answer: '', actions: [{ tool: 'list_tabs', args: '{}' }] } : { answer: 'One tab is open.', actions: [] };
    if (turns === 2) assert.match(request.messages.at(-1).content, /untrusted_tool_results/);
    res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify({ message: { content: JSON.stringify(output) } }));
  });
  await new Promise(r => ollama.listen(0, '127.0.0.1', r));
  t.after(() => new Promise(r => ollama.close(r)));
  const engine = new Engine({ providerFactory: config => new Provider(config, { environment: { ZITTER_OLLAMA_URL: `http://127.0.0.1:${ollama.address().port}` } }) });
  const result = await engine.turn({ id: 'integration', config: { provider: 'ollama', model: 'test-local' }, permissions: { tabs: true }, prompt: 'How many tabs?', context: {}, date: '2026-11-04' }, signal);
  assert.equal(result.actions[0].tool, 'list_tabs');
  const final = await engine.resume('integration', [{ tool: 'list_tabs', result: [{ title: 'Example' }] }], signal);
  assert.equal(final.answer, 'One tab is open.'); assert.equal(turns, 2);
});

test('OpenAI sends a nonstored structured Responses request with cache key', async () => {
  const provider = new Provider({ provider: 'openai', model: 'chosen-model' }, {
    environment: { OPENAI_API_KEY: 'test-only' },
    fetcher: async (url, init) => {
      assert.equal(url, 'https://api.openai.com/v1/responses');
      const body = JSON.parse(init.body);
      assert.equal(body.model, 'chosen-model'); assert.equal(body.store, false);
      assert.equal(body.prompt_cache_key, 'zitter-assistant-v1'); assert.equal(body.text.format.strict, true);
      assert.equal(body.instructions, 'Stable policy');
      return { ok: true, json: async () => ({ output: [{ content: [{ type: 'output_text', text: JSON.stringify(response) }] }] }) };
    },
  });
  assert.equal((await provider.complete('Stable policy', [{ role: 'user', content: 'Hi' }], signal)).answer, 'Ready');
});

test('Ollama uses local schema-constrained chat and a warm model', async () => {
  const provider = new Provider({ provider: 'ollama', model: 'local-model' }, {
    environment: {}, fetcher: async (url, init) => {
      assert.equal(url.href, 'http://127.0.0.1:11434/api/chat');
      const body = JSON.parse(init.body);
      assert.equal(body.stream, false); assert.equal(body.keep_alive, '10m'); assert.equal(body.format.type, 'object');
      return { ok: true, json: async () => ({ message: { content: JSON.stringify(response) } }) };
    },
  });
  assert.equal((await provider.complete('Policy', [], signal)).answer, 'Ready');
  const remote = new Provider({ provider: 'ollama', model: 'local' }, { environment: { ZITTER_OLLAMA_URL: 'https://external.example' } });
  await assert.rejects(remote.complete('', [], signal), /local HTTP/);
});

test('Claude disables built-in tools, MCP, persisted sessions and inherited settings', async () => {
  let directory;
  const provider = new Provider({ provider: 'claude', model: 'sonnet' }, {
    environment: {}, runCLI: async (command, args, input, passedSignal, cwd) => {
      directory = cwd; assert.equal(command, 'claude'); assert.equal(passedSignal, signal);
      assert.equal(args[args.indexOf('--tools') + 1], '');
      assert.equal(args[args.indexOf('--setting-sources') + 1], '');
      assert.equal(args[args.indexOf('--mcp-config') + 1], '{"mcpServers":{}}');
      assert.ok(args.includes('--no-session-persistence')); assert.match(input, /messages/);
      return JSON.stringify({ structured_output: response });
    },
  });
  assert.equal((await provider.complete('Policy', [], signal)).answer, 'Ready');
  await assert.rejects(access(directory));
});

test('Codex SDK isolates its home and disables native shell/network tools', async () => {
  let directory;
  class FakeCodex {
    constructor(options) {
      directory = options.env.CODEX_HOME;
      assert.equal(options.config.features.shell_tool, false);
      assert.deepEqual(options.config.mcp_servers, {});
    }
    startThread(options) {
      assert.equal(options.workingDirectory, directory); assert.equal(options.sandboxMode, 'read-only');
      assert.equal(options.networkAccessEnabled, false); assert.equal(options.webSearchMode, 'disabled');
      return { run: async (prompt, turn) => {
        assert.equal(turn.signal, signal); assert.equal(turn.outputSchema.type, 'object');
        return { finalResponse: JSON.stringify(response) };
      } };
    }
  }
  const provider = new Provider({ provider: 'codex', model: 'chosen-codex' }, {
    environment: { CODEX_HOME: 'nonexistent-test-home' }, sdkLoader: async () => ({ Codex: FakeCodex }),
  });
  assert.equal((await provider.complete('Policy', [], signal)).answer, 'Ready');
  await assert.rejects(access(directory));
});

test('screenshots reach every provider as images, never as transcript text', async () => {
  const messages = [{ role: 'user', content: '{"untrusted_tool_results":[]}', images: ['aW1hZ2U='] }];
  const openai = new Provider({ provider: 'openai', model: 'm' }, { environment: { OPENAI_API_KEY: 'test-only' },
    fetcher: async (url, init) => {
      const content = JSON.parse(init.body).input[0].content;
      assert.equal(content[0].type, 'input_text');
      assert.equal(content[1].image_url, 'data:image/jpeg;base64,aW1hZ2U=');
      return { ok: true, json: async () => ({ output: [{ content: [{ type: 'output_text', text: JSON.stringify(response) }] }] }) };
    } });
  assert.equal((await openai.complete('Policy', messages, signal)).answer, 'Ready');
  const ollama = new Provider({ provider: 'ollama', model: 'm' }, { environment: {},
    fetcher: async (url, init) => {
      assert.deepEqual(JSON.parse(init.body).messages[1].images, ['aW1hZ2U=']);
      return { ok: true, json: async () => ({ message: { content: JSON.stringify(response) } }) };
    } });
  assert.equal((await ollama.complete('Policy', messages, signal)).answer, 'Ready');
  const claude = new Provider({ provider: 'claude', model: 'sonnet' }, { environment: {},
    runCLI: async (command, args, input) => {
      assert.equal(args[args.indexOf('--input-format') + 1], 'stream-json');
      const line = JSON.parse(input);
      assert.doesNotMatch(line.message.content[0].text, /aW1hZ2U=/);
      assert.deepEqual(line.message.content[1].source, { type: 'base64', media_type: 'image/jpeg', data: 'aW1hZ2U=' });
      // After the images, a reminder that browser tools go in actions, not native calls.
      assert.match(line.message.content.at(-1).text, /actions/);
      return `{"type":"system"}\n${JSON.stringify({ type: 'result', structured_output: response })}\n`;
    } });
  assert.equal((await claude.complete('Policy', messages, signal)).answer, 'Ready');
});

test('Claude streams the answer as the model writes it', async () => {
  const { partialAnswer } = await import('./providers.mjs');
  assert.equal(partialAnswer('{"answer": "Ko'), 'Ko');
  assert.equal(partialAnswer('{"answer":"a\nb \\"c\\" \u0105'), 'a\nb "c" ą');
  assert.equal(partialAnswer('{"answer":"x' + '\\'), 'x');
  assert.equal(partialAnswer('{"actions":[]'), '');
  const seen = [];
  const provider = new Provider({ provider: 'claude', model: 'sonnet' }, { environment: {},
    runCLI: async (command, args, input, passedSignal, cwd, onData) => {
      assert.ok(args.includes('--include-partial-messages'));
      assert.equal(args[args.indexOf('--output-format') + 1], 'stream-json');
      const event = partial_json => JSON.stringify({ type: 'stream_event', event: { type: 'content_block_delta', delta: { type: 'input_json_delta', partial_json } } });
      const lines = [event('{"answer": "Czy'), event('tam?", "actions": []}'), JSON.stringify({ type: 'result', structured_output: response })];
      onData(lines[0] + '\n' + lines[1].slice(0, 20)); onData(lines[1].slice(20) + '\n' + lines[2] + '\n');
      return lines.join('\n') + '\n';
    } });
  assert.equal((await provider.complete('Policy', [], signal, text => seen.push(text))).answer, 'Ready');
  assert.deepEqual(seen, ['Czy', 'Czytam?']);
});

test('OpenAI, Ollama and Codex stream the answer too', async () => {
  const body = parts => new ReadableStream({ start(c) { for (const p of parts) c.enqueue(new TextEncoder().encode(p)); c.close(); } });
  const full = JSON.stringify(response);
  const half = full.slice(0, full.indexOf('Ready') + 3);
  const seen = [];
  const openai = new Provider({ provider: 'openai', model: 'm' }, { environment: { OPENAI_API_KEY: 'test-only' },
    fetcher: async (url, init) => {
      assert.equal(JSON.parse(init.body).stream, true);
      const event = value => `data: ${JSON.stringify(value)}\n\n`;
      return { ok: true, body: body([event({ type: 'response.output_text.delta', delta: half }),
        event({ type: 'response.output_text.delta', delta: full.slice(half.length) }).slice(0, 10),
        event({ type: 'response.output_text.delta', delta: full.slice(half.length) }).slice(10)]) };
    } });
  assert.equal((await openai.complete('Policy', [], signal, text => seen.push(text))).answer, 'Ready');
  const ollama = new Provider({ provider: 'ollama', model: 'm' }, { environment: {},
    fetcher: async (url, init) => {
      assert.equal(JSON.parse(init.body).stream, true);
      return { ok: true, body: body([JSON.stringify({ message: { content: half } }) + '\n',
        JSON.stringify({ message: { content: full.slice(half.length) }, done: true }) + '\n']) };
    } });
  assert.equal((await ollama.complete('Policy', [], signal, text => seen.push(text))).answer, 'Ready');
  class StreamingCodex {
    startThread() {
      return { runStreamed: async () => ({ events: (async function* () {
        yield { type: 'item.updated', item: { type: 'agent_message', text: half } };
        yield { type: 'item.completed', item: { type: 'agent_message', text: full } };
        yield { type: 'turn.completed', usage: null };
      })() }) };
    }
  }
  const codex = new Provider({ provider: 'codex', model: 'm' }, { environment: { CODEX_HOME: 'nonexistent-test-home' },
    sdkLoader: async () => ({ Codex: StreamingCodex }) });
  assert.equal((await codex.complete('Policy', [], signal, text => seen.push(text))).answer, 'Ready');
  assert.deepEqual(seen, ['Rea', 'Ready', 'Rea', 'Ready', 'Rea', 'Ready']);
});
