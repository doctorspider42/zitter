// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
import { spawn } from 'node:child_process';
import { mkdtemp, rm, copyFile, chmod } from 'node:fs/promises';
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

function cli(command, args, input, signal, cwd) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { cwd, windowsHide: true, shell: false, signal, stdio: ['pipe', 'pipe', 'pipe'] });
    let output = '', size = 0;
    child.stdout.on('data', data => { size += data.length; if (size > 4 * 1024 * 1024) child.kill(); else output += data; });
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
  async complete(system, messages, signal) {
    const { provider, model } = this.config;
    const env = this.environment;
    if (provider === 'openai') {
      if (!env.OPENAI_API_KEY) throw new AssistantError('Set OPENAI_API_KEY before starting Zitter.');
      const data = await json('https://api.openai.com/v1/responses', {
        method: 'POST', headers: { Authorization: `Bearer ${env.OPENAI_API_KEY}`, 'Content-Type': 'application/json' },
        body: JSON.stringify({ model, store: false, instructions: system,
          input: messages.map(m => ({ role: m.role, content: m.content })),
          text: { format: { type: 'json_schema', name: 'zitter_turn', strict: true, schema } },
          prompt_cache_key: 'zitter-assistant-v1',
        }),
      }, signal, this.fetcher);
      return decode(data.output?.flatMap(i => i.content ?? []).filter(c => c.type === 'output_text').map(c => c.text).join(''));
    }
    if (provider === 'ollama') {
      const endpoint = new URL(env.ZITTER_OLLAMA_URL ?? 'http://127.0.0.1:11434');
      if (!['127.0.0.1', 'localhost', '[::1]'].includes(endpoint.hostname) || endpoint.protocol !== 'http:')
        throw new AssistantError('Ollama must use a local HTTP endpoint.');
      const data = await json(new URL('/api/chat', endpoint), {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ model, stream: false, keep_alive: '10m', format: schema,
          messages: [{ role: 'system', content: system }, ...messages] }),
      }, signal, this.fetcher);
      return decode(data.message?.content);
    }
    // Isolated, empty working directories keep CLI agents away from browser profiles.
    const cwd = await mkdtemp(join(tmpdir(), 'zitter-assistant-'));
    try {
      const input = JSON.stringify({ messages });
      if (provider === 'claude') {
        const output = await this.runCLI(env.ZITTER_CLAUDE_PATH ?? 'claude', [
          '-p', '--model', model, '--output-format', 'json', '--json-schema', JSON.stringify(schema),
          '--system-prompt', system, '--tools', '', '--strict-mcp-config', '--mcp-config', '{"mcpServers":{}}',
          '--no-session-persistence',
          '--setting-sources', '', '--disable-slash-commands',
        ], input, signal, cwd);
        const result = JSON.parse(output);
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
      const result = await thread.run(`${system}\n\n${input}`, { outputSchema: schema, signal });
      return decode(result.finalResponse);
    } finally { await rm(cwd, { recursive: true, force: true }); }
  }
}
