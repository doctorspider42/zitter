#!/usr/bin/env node
// Observe an owned Zitter bundle with synthetic profiles, never the user's data.
// Requires Node >=22 (built-in WebSocket). No third-party dependencies.
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import crypto from 'node:crypto';
import {spawn, execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

// Deliberately retain paths, not queries/credentials, in the shareable summary.
export function endpoint(value) {
  try {
    const url = new URL(value);
    if (!['http:', 'https:', 'ws:', 'wss:'].includes(url.protocol)) return null;
    return {host: url.hostname, endpoint: `${url.protocol}//${url.host}${url.pathname}`};
  } catch { return null; }
}

export function summarizeNetlog(log) {
  const names = Object.fromEntries(Object.entries(log.constants.logEventTypes).map(([k,v]) => [v,k]));
  const phases = log.constants.logEventPhase;
  const requests = new Map();
  const resolutions = new Set();
  const connections = [];
  const certificates = [];
  for (const event of log.events) {
    const name = names[event.type] || String(event.type);
    const params = event.params || {};
    if (name === 'URL_REQUEST_START_JOB' && event.phase === phases.PHASE_BEGIN) {
      const target = endpoint(params.url);
      if (target) {
        const key = `${event.source.type}:${event.source.id}`;
        const record = requests.get(key) || {source_id: event.source.id, hops: []};
        record.hops.push({...target, method: params.method, time: event.time,
                          initiator: params.initiator, network_anonymization_key: params.network_anonymization_key});
        requests.set(key, record);
      }
    }
    if (name === 'URL_REQUEST_REDIRECTED') {
      const record = requests.get(`${event.source.type}:${event.source.id}`);
      const target = endpoint(params.location);
      if (record && target) record.hops.push({...target, redirect: true, time: event.time});
    }
    const record = requests.get(`${event.source.type}:${event.source.id}`);
    const hop = record?.hops.findLast(h => !h.redirect);
    if (record && params.net_error !== undefined) {
      record.net_error = params.net_error;
      if (hop) hop.net_error = params.net_error;
    }
    if (record && name === 'HTTP_TRANSACTION_READ_RESPONSE_HEADERS') {
      record.response_status_lines ??= [];
      for (const line of params.headers || []) if (/^HTTP\//.test(line)) {
        record.response_status_lines.push(line);
        if (hop) {hop.response_status_lines ??= []; hop.response_status_lines.push(line);}
      }
    }
    if (/HOST_RESOLVER.*REQUEST$/.test(name) && params.host) resolutions.add(params.host);
    if (name === 'TCP_CONNECT_ATTEMPT' && event.phase === phases.PHASE_BEGIN) {
      connections.push({source_id: event.source.id, address: params.address, time: event.time});
    }
    if (name === 'SSL_CONNECT' && event.phase === phases.PHASE_END) {
      certificates.push({source_id: event.source.id, net_error: params.net_error ?? 0,
                         version: params.version, time: event.time});
    }
  }
  const rows = [...requests.values()];
  const hosts = {};
  for (const row of rows) for (const hop of row.hops.filter(h => !h.redirect)) {
    const host = hosts[hop.host] ??= {requests: 0, endpoints: [], response_status_lines: [], net_errors: []};
    host.requests++;
    if (!host.endpoints.includes(hop.endpoint)) host.endpoints.push(hop.endpoint);
    for (const status of hop.response_status_lines || []) if (!host.response_status_lines.includes(status)) host.response_status_lines.push(status);
    if (hop.net_error !== undefined && !host.net_errors.includes(hop.net_error)) host.net_errors.push(hop.net_error);
  }
  return {event_count: log.events.length, hosts, requests: rows,
          dns_requests: [...resolutions].sort(), tcp_connect_attempts: connections, tls_results: certificates};
}

async function cdp(url) {
  const ws = new WebSocket(url);
  const pending = new Map();
  let next = 0;
  await new Promise((resolve, reject) => {ws.onopen = resolve; ws.onerror = reject;});
  ws.onmessage = event => {
    const response = JSON.parse(event.data);
    const operation = pending.get(response.id);
    if (!operation) return;
    pending.delete(response.id); clearTimeout(operation.timer);
    if (response.error) operation.reject(new Error(JSON.stringify(response.error)));
    else operation.resolve(response.result);
  };
  return {
    send(method, params = {}, sessionId) {
      return new Promise((resolve, reject) => {
        const id = ++next;
        const timer = setTimeout(() => {pending.delete(id); reject(new Error(`CDP timeout: ${method}`));}, 10000);
        pending.set(id, {resolve, reject, timer});
        ws.send(JSON.stringify({id, method, params, ...(sessionId ? {sessionId} : {})}));
      });
    },
    close() {ws.close(); for (const op of pending.values()) {clearTimeout(op.timer); op.reject(new Error('CDP closed'));} pending.clear();}
  };
}

function fixture(directory, url = '') {
  fs.mkdirSync(directory);
  const profiles = [{id: 'work-profile', name: 'Work', directory: 'ZitterWork'},
                    {id: 'personal-profile', name: 'Personal', directory: 'ZitterPersonal'}];
  const workspaces = profiles.map((p, i) => ({id: i ? 'personal' : 'work', name: p.name, profile_id: p.id}));
  const session = Object.fromEntries(workspaces.map(w => [w.id, {
    profile_id: w.profile_id, active: 0, left: 0, right: -1, ratio: 0.5,
    tabs: [{url, title: 'Network audit fixture'}]
  }]));
  for (const [name, value] of Object.entries({
    ZitterWorkspaces: {profiles, workspaces, active: 'work', onboarding_complete: true},
    ZitterSession: session, ZitterTimeMachine: {}
  })) fs.writeFileSync(path.join(directory, `${name}.json`), JSON.stringify(value));
}

async function runScenario(executable, output, spec) {
  const netlog = path.join(output, `${spec.name}.netlog.json`);
  const stderr = path.join(output, `${spec.name}.stderr.log`);
  const command = [executable, `--user-data-dir=${spec.profile}`, '--no-first-run',
    '--no-default-browser-check', '--use-mock-keychain', '--remote-debugging-port=0',
    `--log-net-log=${netlog}`, '--net-log-capture-mode=Default', '--enable-logging=stderr',
    ...(spec.flags || [])];
  const fd = fs.openSync(stderr, 'w', 0o600);
  const started = new Date().toISOString();
  const process = spawn(executable, command.slice(1), {stdio: ['ignore', fd, fd], detached: true});
  fs.closeSync(fd);
  let client;
  const record = {name: spec.name, started_utc: started, duration_seconds: spec.seconds, command,
                  profile: spec.profile, actions: [], artificial_keychain: true};
  if (spec.testThreat) record.injected_test_threat = spec.testThreat;
  try {
    let port;
    for (let i = 0; i < 600; i++) {
      if (process.exitCode !== null) throw new Error(`Browser exited early: ${process.exitCode}`);
      try {port = fs.readFileSync(path.join(spec.profile, 'DevToolsActivePort'), 'utf8').split('\n');} catch {}
      if (port?.[0]) break;
      await sleep(200);
    }
    if (!port?.[0]) throw new Error('DevToolsActivePort not created');
    client = await cdp(`ws://127.0.0.1:${port[0]}${port[1]}`);
    record.browser = await client.send('Browser.getVersion');
    const observationStarted = Date.now();
    record.startup_seconds = (observationStarted - Date.parse(started)) / 1000;
    const downloadPath = path.join(output, `${spec.name}-downloads`);
    fs.mkdirSync(downloadPath);
    await client.send('Browser.setDownloadBehavior', {behavior: 'allow', downloadPath});
    record.download_directory = downloadPath;
    console.log(`${spec.name}: running pid=${process.pid} for ${spec.seconds}s`);
    await sleep(Math.min((spec.warmupSeconds ?? 10) * 1000, spec.seconds * 1000 / 1.5));
    if (spec.actions) {
      const {targetInfos} = await client.send('Target.getTargets');
      const target = targetInfos.find(t => t.type === 'page' && t.url.startsWith(spec.localOrigin));
      if (!target) throw new Error('Fixture page not found; feature scenario invalid');
      const {sessionId} = await client.send('Target.attachToTarget', {targetId: target.targetId, flatten: true});
      for (const action of spec.actions) {
        const result = await client.send('Runtime.evaluate', {expression: action.expression, awaitPromise: true, returnByValue: true, userGesture: true}, sessionId);
        record.actions.push({name: action.name, result, at_utc: new Date().toISOString()});
      }
    }
    await sleep(Math.max(0, spec.seconds * 1000 - (Date.now() - observationStarted)));
    record.targets = (await client.send('Target.getTargets')).targetInfos.map(t => ({type: t.type, url: endpoint(t.url)?.endpoint || t.url}));
    if (spec.inspectPage) {
      const {targetInfos} = await client.send('Target.getTargets');
      const target = targetInfos.find(t => t.type === 'page' && t.url.startsWith(spec.inspectPage));
      if (target) {
        const {sessionId} = await client.send('Target.attachToTarget', {targetId: target.targetId, flatten: true});
        record.page_inspection = await client.send('Runtime.evaluate', {
          expression: '({url:location.href,title:document.title,text:document.body?.innerText.slice(0,4000)})', returnByValue: true
        }, sessionId);
        record.navigation_history = await client.send('Page.getNavigationHistory', {}, sessionId);
        const capture = await client.send('Page.captureScreenshot', {format: 'png'}, sessionId);
        record.screenshot = path.join(output, `${spec.name}.png`);
        fs.writeFileSync(record.screenshot, Buffer.from(capture.data, 'base64'));
        if (spec.verifyWarning) {
          const value = record.page_inspection.result?.value;
          const warningShown = value?.url === 'chrome-error://chromewebdata/' &&
            value?.text?.includes('Dangerous site') && !value?.text?.includes('Phishing Page Example.');
          record.warning_assertion = {passed: Boolean(warningShown), injected: Boolean(spec.testThreat)};
          if (!warningShown) throw new Error('Expected Safe Browsing warning was not displayed');
          await client.send('Runtime.evaluate', {
            expression: 'document.getElementById("primary-button").click()', userGesture: true
          }, sessionId);
          await sleep(2000);
          record.back_to_safety = await client.send('Runtime.evaluate', {
            expression: '({url:location.href,text:document.body?.innerText.slice(0,200)})', returnByValue: true
          }, sessionId);
          if (record.back_to_safety.result?.value?.url !== `${spec.localOrigin}/fixture`)
            throw new Error('Back to safety failed to return to the previous safe page');
        }
      } else {
        record.page_inspection = {error: 'Expected target not found'};
        if (spec.verifyWarning) throw new Error('Expected Safe Browsing target not found');
      }
    }
    try {await client.send('Browser.close');} catch (error) {record.close_note = error.message;}
    for (let i = 0; i < 100 && process.exitCode === null; i++) await sleep(100);
    if (process.exitCode === null) {record.forced_termination = true; process.kill('SIGTERM');}
    for (let i = 0; i < 50 && process.exitCode === null; i++) await sleep(100);
  } catch (error) {
    record.error = error.stack;
  } finally {
    // Preserve a complete NetLog even when a security assertion fails.
    if (client && process.exitCode === null && process.signalCode === null) {
      try {await client.send('Browser.close');} catch {}
      for (let i = 0; i < 100 && process.exitCode === null && process.signalCode === null; i++) await sleep(100);
    }
    client?.close();
    if (process.exitCode === null && process.signalCode === null) {
      record.forced_termination = true;
      try {globalThis.process.kill(-process.pid, 'SIGKILL');} catch {}
    }
    record.exit_code = process.exitCode; record.signal = process.signalCode;
    record.ended_utc = new Date().toISOString();
    try {
      record.network = summarizeNetlog(JSON.parse(fs.readFileSync(netlog, 'utf8')));
      record.netlog_complete = true;
    } catch (error) {record.netlog_complete = false; record.netlog_error = error.message;}
    fs.writeFileSync(path.join(output, `${spec.name}.summary.json`), JSON.stringify(record, null, 2));
    console.log(`${spec.name}: ${JSON.stringify({error: record.error, complete: record.netlog_complete, hosts: record.network?.hosts})}`);
  }
  return record;
}

async function main() {
  const args = globalThis.process.argv.slice(2);
  function option(name, fallback) {const index = args.indexOf(name); return index < 0 ? fallback : args[index + 1];}
  if (args.includes('--help')) {
    console.log('node tools/audit_background_network.mjs [--app PATH] [--output NEW_DIRECTORY] [--seconds 120] [--external-url HTTPS_URL] [--inspect-page] [--verify-warning] [--warmup-seconds 10] [--test-threat phishing|malware] [--suppression-control] [--targeted-probe] [--only SCENARIO,SCENARIO]');
    return;
  }
  const app = path.resolve(option('--app', path.join(root, 'build/Zitter.app')));
  const executable = path.join(app, 'Contents/MacOS/Chromium');
  if (!fs.existsSync(path.join(app, 'Contents/Resources/zitter-development-bundle')) &&
      !fs.existsSync(path.join(app, 'Contents/zitter-development-bundle'))) throw new Error('Expected marker-owned Zitter bundle');
  const output = path.resolve(option('--output', path.join(root, 'build/network-audit', new Date().toISOString().replaceAll(':', '-'))));
  const seconds = Number(option('--seconds', '120'));
  if (!Number.isFinite(seconds) || seconds < 15 || seconds > 3600) throw new Error('--seconds must be 15..3600');
  fs.mkdirSync(path.dirname(output), {recursive: true});
  // Real API-key runs put query credentials in the raw NetLog. Keep the whole
  // audit directory private, including files created directly by Chromium.
  fs.mkdirSync(output, {mode: 0o700});
  const manifestPath = path.join(output, 'manifest.json');
  if (fs.existsSync(manifestPath)) throw new Error('Output already contains an audit');
  const manifest = {created_utc: new Date().toISOString(), executable,
    sha256: crypto.createHash('sha256').update(fs.readFileSync(executable)).digest('hex'),
    binary_mtime: fs.statSync(executable).mtime.toISOString(),
    source_revision_lock: JSON.parse(fs.readFileSync(path.join(root, 'chromium/revision.json'), 'utf8')),
    source_git_head: execFileSync('git', ['rev-parse', 'HEAD'], {cwd: root, encoding: 'utf8'}).trim(),
    chromium_checkout_head: execFileSync('git', ['rev-parse', 'HEAD'], {cwd: path.join(root, '.chromium/src'), encoding: 'utf8'}).trim(),
    inherited_google_credential_variables_present: Object.fromEntries(
      ['GOOGLE_API_KEY', 'GOOGLE_DEFAULT_CLIENT_ID', 'GOOGLE_DEFAULT_CLIENT_SECRET'].map(key => [key, Boolean(globalThis.process.env[key])])),
    note: 'Runner uses the supplied bundle without rebuilding. Isolated synthetic profiles, mock Keychain, local downloads, default NetLog. Application policy applies; runner suppression or injected verdicts are explicitly labelled.', scenarios: []};
  fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2));
  const hits = [];
  const server = http.createServer((req, res) => {
    hits.push({time: new Date().toISOString(), method: req.method, path: req.url});
    res.setHeader('Cache-Control', 'no-store');
    if (req.url === '/download') {
      res.setHeader('Content-Disposition', 'attachment; filename="zitter-audit.txt"');
      res.setHeader('Content-Type', 'text/plain'); res.end('Synthetic Zitter audit download.\n'); return;
    }
    res.setHeader('Content-Type', 'text/html; charset=utf-8');
    res.end('<!doctype html><title>Zitter network audit</title><h1>Local-only fixture</h1><form><input name="name" autocomplete="name"><input name="email" autocomplete="email"></form>');
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  const blank = path.join(output, 'profile-ready'); fixture(blank);
  const local = path.join(output, 'profile-local'); fixture(local, `${origin}/fixture`);
  const specs = [
    {name: 'fresh-onboarding', profile: path.join(output, 'profile-onboarding'), seconds},
    {name: 'fresh-ready-idle', profile: blank, seconds},
    {name: 'warm-ready-idle', profile: blank, seconds},
    {name: 'local-features', profile: local, seconds, localOrigin: origin, actions: [
      {name: 'local-fetch', expression: 'fetch("/fetch").then(r=>r.status)'},
      {name: 'local-download', expression: '(()=>{const a=document.createElement("a");a.href="/download";a.click();return "clicked"})()'},
      {name: 'local-form-input', expression: '(()=>{const e=document.querySelector("input");e.focus();e.value="Audit Person";e.dispatchEvent(new Event("input",{bubbles:true}));return e.value})()'}
    ]}
  ];
  const external = option('--external-url', '');
  const testThreat = option('--test-threat', '');
  const verifyWarning = args.includes('--verify-warning');
  const warmupSeconds = Number(option('--warmup-seconds', '10'));
  if (!Number.isFinite(warmupSeconds) || warmupSeconds < 0 || warmupSeconds >= seconds) throw new Error('Warmup must be shorter than the scenario');
  if (testThreat && (!external || !['phishing', 'malware'].includes(testThreat))) throw new Error('--test-threat requires an external URL and phishing|malware');
  if (external) {
    if (!external.startsWith('https://')) throw new Error('External URL must use HTTPS');
    const directory = path.join(output, 'profile-external'); fixture(directory, verifyWarning ? `${origin}/fixture` : external);
    specs.push({name: 'external-navigation', profile: directory, seconds,
                flags: testThreat ? [`--mark_as_${testThreat}=${external}`] : [], testThreat,
                inspectPage: (args.includes('--inspect-page') || verifyWarning) ? external : undefined,
                verifyWarning, warmupSeconds, localOrigin: origin,
                actions: verifyWarning ? [{name: 'navigate-to-threat', expression: `location.assign(${JSON.stringify(external)})`}] : undefined});
  }
  if (args.includes('--suppression-control')) {
    const directory = path.join(output, 'profile-suppressed'); fixture(directory);
    specs.push({name: 'suppressed-control', profile: directory, seconds,
                flags: ['--disable-background-networking', '--disable-component-update']});
  }
  if (args.includes('--targeted-probe')) {
    // Experimental command-line overrides, not a shipping privacy policy.
    // Keep component updates and Safe Browsing enabled in this comparison.
    const flags = ['--disable-features=AimEnabled,NetworkTimeServiceQuerying,AutofillServerCommunication,TranslateRankerQuery,TranslateRankerEnforcement'];
    const directory = path.join(output, 'profile-targeted-idle'); fixture(directory);
    specs.push({name: 'targeted-idle', profile: directory, seconds, flags});
    const featureDirectory = path.join(output, 'profile-targeted-local'); fixture(featureDirectory, `${origin}/fixture`);
    const featureSpec = specs.find(s => s.name === 'local-features');
    specs.push({...featureSpec, name: 'targeted-local', profile: featureDirectory, flags});
  }
  const only = option('--only', '').split(',').filter(Boolean);
  if (only.some(name => !specs.some(s => s.name === name))) throw new Error('Unknown --only scenario');
  try {
    for (const spec of specs.filter(s => !only.length || only.includes(s.name))) {
      // A previous run's port file must not be mistaken for the new process.
      fs.rmSync(path.join(spec.profile, 'DevToolsActivePort'), {force: true});
      manifest.scenarios.push(await runScenario(executable, output, spec));
      fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2));
    }
  } finally {
    fs.writeFileSync(path.join(output, 'local-server-hits.json'), JSON.stringify(hits, null, 2));
    server.close();
  }
  console.log(`Audit saved: ${output}`);
  if (manifest.scenarios.some(s => s.error || !s.netlog_complete || s.forced_termination)) globalThis.process.exitCode = 1;
}

if (globalThis.process.argv[1] && path.resolve(globalThis.process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => {console.error(error); globalThis.process.exitCode = 1;});
}
