// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
import { spawn } from 'node:child_process';
import { mkdtemp, rm, copyFile, chmod, writeFile } from 'node:fs/promises';
import { tmpdir, homedir } from 'node:os';
import { join } from 'node:path';
import { AssistantError } from './errors.mjs';
import { setTimeout as delay } from 'node:timers/promises';
import { pathToFileURL } from 'node:url';

export const schema = {
  type: 'object', additionalProperties: false, required: ['answer', 'actions'],
  properties: {
    answer: { type: 'string' },
    actions: { type: 'array', maxItems: 16, items: {
      type: 'object', additionalProperties: false, required: ['tool', 'args'],
      properties: { tool: { type: 'string' }, args: { type: 'string', description: 'JSON encoded tool arguments' } },
    } },
  },
};

// The "answer" string of a JSON object still being written, decoded so far.
export function partialAnswer(text) {
  const key = text.search(/"answer"\s*:\s*"/);
  if (key < 0) return '';
  let i = text.indexOf('"', text.indexOf(':', key + 8)) + 1, out = '';
  while (i < text.length) {
    const c = text[i];
    if (c === '"') break;
    if (c !== '\\') { out += c; i++; continue; }
    if (i + 1 >= text.length) break;
    const escape = text[i + 1];
    if (escape === 'u') {
      if (i + 6 > text.length) break;
      out += String.fromCharCode(parseInt(text.slice(i + 2, i + 6), 16)); i += 6; continue;
    }
    out += { n: '\n', t: '\t', r: '\r', b: '\b', f: '\f' }[escape] ?? escape; i += 2;
  }
  return out;
}

export function decode(text) {
  const value = typeof text === 'string' ? JSON.parse(text) : text;
  if (!value || typeof value.answer !== 'string' || !Array.isArray(value.actions) || value.actions.length > 16)
    throw new AssistantError('The model returned an invalid assistant response. Try another tool-capable model.');
  for (const a of value.actions) {
    if (typeof a.tool !== 'string' || typeof a.args !== 'string') throw new AssistantError('Invalid tool call');
    a.args = JSON.parse(a.args);
    if (!a.args || Array.isArray(a.args) || typeof a.args !== 'object') throw new AssistantError('Invalid tool arguments');
  }
  return value;
}

async function json(url, init, signal, fetcher) {
  for (let attempt = 0; attempt < 3; attempt++) {
    const response = await fetcher(url, { ...init, signal: AbortSignal.any([signal, AbortSignal.timeout(180000)]) });
    if (response.ok) return response.json();
    if (attempt < 2 && [429, 500, 502, 503, 504].includes(response.status)) {
      const retry = Number(response.headers?.get('retry-after'));
      await response.body?.cancel();
      await delay(Math.min(10000, Math.max(250, retry * 1000 || 500 * 2 ** attempt)), undefined, { signal });
      continue;
    }
    throw new AssistantError(`Provider request failed (${response.status}). Check credentials, endpoint and model.`);
  }
}

// A streamed HTTP response, line by line (Server-Sent Events or NDJSON), with
// the same retries as json(). Responses without a body stream arrive whole.
async function lines(url, init, signal, fetcher, onLine) {
  for (let attempt = 0; attempt < 3; attempt++) {
    const response = await fetcher(url, { ...init, signal: AbortSignal.any([signal, AbortSignal.timeout(180000)]) });
    if (response.ok) {
      let pending = '';
      const feed = text => {
        pending += text;
        const parts = pending.split('\n'); pending = parts.pop();
        for (const part of parts) if (part.trim()) onLine(part.trim());
      };
      if (response.body?.getReader) {
        const reader = response.body.getReader(), decoder = new TextDecoder();
        for (;;) { const { done, value } = await reader.read(); if (done) break; feed(decoder.decode(value, { stream: true })); }
      } else feed(typeof response.text === 'function' ? await response.text() : JSON.stringify(await response.json()));
      if (pending.trim()) onLine(pending.trim());
      return;
    }
    if (attempt < 2 && [429, 500, 502, 503, 504].includes(response.status)) {
      const retry = Number(response.headers?.get('retry-after'));
      await response.body?.cancel();
      await delay(Math.min(10000, Math.max(250, retry * 1000 || 500 * 2 ** attempt)), undefined, { signal });
      continue;
    }
    throw new AssistantError(`Provider request failed (${response.status}). Check credentials, endpoint and model.`);
  }
}

// Calls `onPartial` with the answer each time the JSON written so far adds to it.
function answerWatcher(onPartial) {
  let shown = '';
  return written => {
    const answer = partialAnswer(written);
    if (answer && answer !== shown) { shown = answer; onPartial(answer); }
  };
}

function cli(command, args, input, signal, cwd, onData) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { cwd, windowsHide: true, shell: false, signal, stdio: ['pipe', 'pipe', 'pipe'] });
    let output = '', size = 0;
    child.stdout.on('data', data => {
      size += data.length;
      if (size > 4 * 1024 * 1024) { child.kill(); return; }
      output += data; onData?.(String(data));
    });
    // Never forward provider diagnostics: they may contain request data or credentials.
    child.stderr.resume();
    child.on('error', () => reject(new AssistantError('Provider CLI could not start. Check installation and login.')));
    child.on('close', code => code === 0 ? resolve(output) : reject(new AssistantError('Provider CLI failed. Check installation and login.')));
    child.stdin.on('error', () => {});
    child.stdin.end(input);
  });
}

export class Provider {
  constructor(config, { fetcher = fetch, runCLI = cli, sdkLoader, environment = process.env } = {}) {
    this.config = config;
    this.fetcher = fetcher; this.runCLI = runCLI; this.environment = environment;
    this.sdkLoader = sdkLoader ?? (() => import(environment.ZITTER_CODEX_SDK_PATH
      ? pathToFileURL(environment.ZITTER_CODEX_SDK_PATH).href : '@openai/codex-sdk'));
    if (!['openai', 'ollama', 'codex', 'claude'].includes(config.provider)) throw new AssistantError('Unknown provider');
    if (!/^[\w][\w.:/@+-]{0,159}$/.test(config.model)) throw new AssistantError('Choose a valid model ID');
  }
  // `onPartial` receives the answer as it is written, where the backend streams.
  async complete(system, messages, signal, onPartial) {
    const { provider, model } = this.config;
    const env = this.environment;
    if (provider === 'openai') {
      if (!env.OPENAI_API_KEY) throw new AssistantError('Set OPENAI_API_KEY before starting Zitter.');
      const request = {
        method: 'POST', headers: { Authorization: `Bearer ${env.OPENAI_API_KEY}`, 'Content-Type': 'application/json' },
        body: JSON.stringify({ model, store: false, instructions: system, stream: !!onPartial,
          input: messages.map(m => ({ role: m.role, content: m.images?.length
            ? [{ type: 'input_text', text: m.content }, ...m.images.map(image => ({ type: 'input_image', image_url: `data:image/jpeg;base64,${image}` }))]
            : m.content })),
          text: { format: { type: 'json_schema', name: 'zitter_turn', strict: true, schema } },
          prompt_cache_key: 'zitter-assistant-v1',
        }),
      };
      const text = output => output?.flatMap(i => i.content ?? []).filter(c => c.type === 'output_text').map(c => c.text).join('');
      if (!onPartial) return decode(text((await json('https://api.openai.com/v1/responses', request, signal, this.fetcher)).output));
      // Server-Sent Events: text deltas, then the completed response.
      let written = '', final = null;
      const watch = answerWatcher(onPartial);
      await lines('https://api.openai.com/v1/responses', request, signal, this.fetcher, line => {
        if (!line.startsWith('data:')) return;
        let event;
        try { event = JSON.parse(line.slice(5)); } catch { return; }
        if (event.type === 'response.output_text.delta') { written += event.delta ?? ''; watch(written); }
        else if (event.type === 'response.completed') final = text(event.response?.output);
        else if (event.type === 'response.failed' || event.type === 'error') throw new AssistantError('OpenAI could not complete the request.');
      });
      return decode(final || written);
    }
    if (provider === 'ollama') {
      const endpoint = new URL(env.ZITTER_OLLAMA_URL ?? 'http://127.0.0.1:11434');
      if (!['127.0.0.1', 'localhost', '[::1]'].includes(endpoint.hostname) || endpoint.protocol !== 'http:')
        throw new AssistantError('Ollama must use a local HTTP endpoint.');
      const request = {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ model, stream: !!onPartial, keep_alive: '10m', format: schema,
          messages: [{ role: 'system', content: system },
            ...messages.map(m => m.images?.length ? { role: m.role, content: m.content, images: m.images } : { role: m.role, content: m.content })] }),
      };
      if (!onPartial) return decode((await json(new URL('/api/chat', endpoint), request, signal, this.fetcher)).message?.content);
      // NDJSON: one chunk of the message per line.
      let written = '';
      const watch = answerWatcher(onPartial);
      await lines(new URL('/api/chat', endpoint), request, signal, this.fetcher, line => {
        let chunk;
        try { chunk = JSON.parse(line); } catch { return; }
        if (chunk.error) throw new AssistantError('Ollama could not complete the request.');
        written += chunk.message?.content ?? ''; watch(written);
      });
      return decode(written);
    }
    // Isolated, empty working directories keep CLI agents away from browser profiles.
    const cwd = await mkdtemp(join(tmpdir(), 'zitter-assistant-'));
    try {
      // Screenshots travel beside the transcript, as the CLI's own image input.
      const images = messages.flatMap(m => m.images ?? []);
      const input = JSON.stringify({ messages: messages.map(({ role, content }) => ({ role, content })) });
      if (provider === 'claude') {
        const streaming = images.length || onPartial;
        // Stream events carry the structured answer as JSON fragments.
        let pending = '', written = '';
        const watch = onPartial && answerWatcher(onPartial);
        const onData = onPartial ? chunk => {
          pending += chunk;
          const lines = pending.split('\n'); pending = lines.pop();
          for (const line of lines) {
            let event;
            try { event = JSON.parse(line); } catch { continue; }
            if (event?.type === 'stream_event' && event.event?.type === 'message_start') written = '';
            const delta = event?.type === 'stream_event' && event.event?.type === 'content_block_delta' ? event.event.delta : null;
            if (delta?.type === 'input_json_delta') written += delta.partial_json ?? '';
            else if (delta?.type === 'text_delta') written += delta.text ?? '';
            else continue;
            watch(written);
          }
        } : undefined;
        const output = await this.runCLI(env.ZITTER_CLAUDE_PATH ?? 'claude', [
          '-p', '--model', model, ...(images.length ? ['--input-format', 'stream-json'] : []),
          ...(streaming ? ['--output-format', 'stream-json', '--verbose'] : ['--output-format', 'json']),
          ...(onPartial ? ['--include-partial-messages'] : []),
          '--json-schema', JSON.stringify(schema),
          '--system-prompt', system, '--tools', '', '--strict-mcp-config', '--mcp-config', '{"mcpServers":{}}',
          '--no-session-persistence',
          '--setting-sources', '', '--disable-slash-commands',
        ], images.length ? JSON.stringify({ type: 'user', message: { role: 'user', content: [{ type: 'text', text: input },
            ...images.map(data => ({ type: 'image', source: { type: 'base64', media_type: 'image/jpeg', data } }))] } }) + '\n' : input, signal, cwd, onData);
        const result = streaming
          ? output.split('\n').map(line => { try { return JSON.parse(line); } catch { return null; } }).find(line => line?.type === 'result') ?? { is_error: true }
          : JSON.parse(output);
        if (result.is_error) throw new AssistantError('Claude could not complete the request.');
        return decode(result.structured_output ?? result.result);
      }
      let Codex;
      try { ({ Codex } = await this.sdkLoader()); }
      catch { throw new AssistantError('Codex SDK installation missing. Run npm install in the assistant runtime directory.'); }
      // Do not inherit user MCP servers, hooks, project instructions or transcript storage.
      try {
        await copyFile(join(env.CODEX_HOME ?? join(homedir(), '.codex'), 'auth.json'), join(cwd, 'auth.json'));
        await chmod(join(cwd, 'auth.json'), 0o600);
      } catch (e) { if (e.code !== 'ENOENT') throw e; }
      const codex = new Codex({ env: { ...env, CODEX_HOME: cwd }, config: {
        shell_environment_policy: { inherit: 'none' },
        features: { shell_tool: false },
        mcp_servers: {},
      } });
      const thread = codex.startThread({ model, workingDirectory: cwd, skipGitRepoCheck: true,
        sandboxMode: 'read-only', approvalPolicy: 'never', networkAccessEnabled: false,
        webSearchMode: 'disabled',
      });
      const files = [];
      for (const [index, image] of images.entries()) {
        const path = join(cwd, `screenshot-${index}.jpg`);
        await writeFile(path, Buffer.from(image, 'base64'));
        files.push({ type: 'local_image', path });
      }
      const prompt = files.length ? [{ type: 'text', text: `${system}\n\n${input}` }, ...files] : `${system}\n\n${input}`;
      if (!onPartial || !thread.runStreamed) return decode((await thread.run(prompt, { outputSchema: schema, signal })).finalResponse);
      // Agent message items grow as Codex writes; the last one is the answer.
      let written = '';
      const watch = answerWatcher(onPartial);
      const { events } = await thread.runStreamed(prompt, { outputSchema: schema, signal });
      for await (const event of events) {
        if ((event.type === 'item.updated' || event.type === 'item.completed') && event.item?.type === 'agent_message') {
          written = event.item.text ?? ''; watch(written);
        } else if (event.type === 'turn.failed' || event.type === 'error') throw new AssistantError('Codex could not complete the request.');
      }
      return decode(written);
    } finally { await rm(cwd, { recursive: true, force: true }); }
  }
}
