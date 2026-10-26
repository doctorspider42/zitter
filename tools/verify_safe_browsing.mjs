#!/usr/bin/env node
// Diagnostic verdict tests verify Chromium's blocking path. Only --live can
// verify the Google-backed protection required before publishing a release.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export function assessRecord(status, record, live) {
  const listUpdateSucceeded = record?.network?.requests?.some(request => request.hops.some(hop =>
    hop.host === 'safebrowsing.googleapis.com' &&
    /\/(?:v4\/threatListUpdates:fetch|v5\/hashLists:batchGet)$/.test(new URL(hop.endpoint).pathname) &&
    hop.response_status_lines?.some(line => /^HTTP\/\S+ 200(?: |$)/.test(line))));
  const passed = status === 0 && record?.netlog_complete &&
    record?.warning_assertion?.passed && record.warning_assertion.injected === !live &&
    Boolean(record.back_to_safety?.result?.value?.url?.startsWith('http://127.0.0.1:')) &&
    (!live || listUpdateSucceeded);
  return {passed: Boolean(passed), list_update_succeeded: Boolean(listUpdateSucceeded)};
}

function main() {
  const args = process.argv.slice(2);
  function option(name, fallback) {
    const index = args.indexOf(name);
    if (index < 0) return fallback;
    if (!args[index + 1] || args[index + 1].startsWith('--')) throw new Error(`Missing ${name}`);
    return args[index + 1];
  }
  if (args.includes('--help')) {
    console.log('node tools/verify_safe_browsing.mjs [--live] [--app PATH] [--output NEW_DIRECTORY]');
    process.exit(0);
  }
  const live = args.includes('--live');
  const output = path.resolve(option('--output', path.join(root, 'build/security', `sb-${Date.now()}`)));
  const app = path.resolve(option('--app', path.join(root, 'build/ZitterSecurity.app')));
  fs.mkdirSync(path.dirname(output), {recursive: true});
  fs.mkdirSync(output, {mode: 0o700});
  const results = [];
  for (const threat of ['phishing', 'malware']) {
    const directory = path.join(output, threat);
    const command = [path.join(root, 'tools/audit_background_network.mjs'), '--app', app,
      '--output', directory, '--only', 'external-navigation', '--verify-warning',
      '--external-url', `https://testsafebrowsing.appspot.com/s/${threat}.html`,
      '--seconds', live ? '350' : '45', '--warmup-seconds', live ? '310' : '10'];
    if (!live) command.push('--test-threat', threat);
    const run = spawnSync(process.execPath, command, {cwd: root, stdio: 'inherit'});
    let record;
    try {record = JSON.parse(fs.readFileSync(path.join(directory, 'external-navigation.summary.json')));} catch {}
    results.push({threat, ...assessRecord(run.status, record, live), injected: !live,
      evidence: directory});
  }
  fs.writeFileSync(path.join(output, 'verification.json'), JSON.stringify({live, results}, null, 2));
  console.log(`${live ? 'LIVE release gate' : 'INJECTED diagnostic only'}: ${results.every(r => r.passed) ? 'PASS' : 'FAIL'}`);
  if (!live) console.log('Real-list protection remains unverified. Run --live with a valid project API key before release.');
  if (!results.every(r => r.passed)) process.exitCode = 1;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
