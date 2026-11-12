// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

import {spawn} from 'node:child_process';
import {readFile, realpath, unlink} from 'node:fs/promises';
import {basename, dirname, join} from 'node:path';
import {tmpdir} from 'node:os';
import {setTimeout as sleep} from 'node:timers/promises';

const [executable, profile, packagePath, extensionId, workUrl, action,
  profileDirectory = 'ZitterWork', companionPath = '-', counterArgument = '41', alarmTime, storageMode] =
  process.argv.slice(2);
const temporaryRoot = await realpath(tmpdir());
const profilePath = await realpath(profile);
const passwordProbe = /^(seed|read)-password(-personal)?$/.test(action);
const settingsProbe = ['disable-password-saving', 'read-password-saving-disabled'].includes(action);
const clipboardProbe = /^(seed|read)-clipboard-(allow|block)$/.test(action);
const storageProbe = /^(seed|read)-storage-(work|personal)$/.test(action);
const fixturePath = passwordProbe || settingsProbe || clipboardProbe || storageProbe
  ? null : await realpath(packagePath);
const companion = fixturePath && companionPath !== '-' ? await realpath(companionPath) : null;
const fixtures = [fixturePath, companion].filter(Boolean);
const counter = Number(counterArgument);
const work = new URL(workUrl);
if (dirname(profilePath) !== temporaryRoot ||
    !basename(profilePath).startsWith('zitter-content-legacy-') ||
    !['ZitterWork', 'ZitterPersonal'].includes(profileDirectory) ||
    (fixturePath && (
      fixtures.some(path => dirname(path) !== temporaryRoot ||
        !basename(path).startsWith('zitter-content-extension-') || path.includes(',')) ||
      companion === fixturePath || !Number.isSafeInteger(counter) || counter < 0 ||
      !/^[a-p]{32}$/.test(extensionId) ||
      !['seed', 'read', 'seed-disabled'].includes(action)
    )) || work.hostname !== '127.0.0.1' || work.protocol !== 'http:') {
  throw new Error('Expected an isolated legacy profile and loopback fixture');
}

try {
  await unlink(join(profilePath, 'DevToolsActivePort'));
} catch (error) {
  if (error.code !== 'ENOENT')
    throw error;
}

const keychainLibrary = process.env.ZITTER_CONTENT_TEST_KEYCHAIN_LIBRARY;
const browserEnvironment = {...process.env};
if (keychainLibrary) {
  const copyRoot = dirname(dirname(dirname(dirname(await realpath(executable)))));
  if (dirname(copyRoot) !== temporaryRoot ||
      !basename(copyRoot).startsWith('zitter-content-legacy-app-') ||
      !(passwordProbe || settingsProbe || clipboardProbe || storageProbe)) {
    throw new Error('Expected an isolated copy for the legacy file Keychain probe');
  }
  browserEnvironment.DYLD_INSERT_LIBRARIES = keychainLibrary;
  delete browserEnvironment.ZITTER_CONTENT_TEST_KEYCHAIN_LIBRARY;
}

const child = spawn(executable, [
  `--user-data-dir=${profilePath}`, `--profile-directory=${profileDirectory}`,
  ...(fixturePath ? [`--load-extension=${fixtures.join(',')}`] : []), '--no-first-run',
  '--no-default-browser-check', '--disable-background-networking',
  ...(!keychainLibrary && process.platform !== 'win32' ? ['--use-mock-keychain'] : []),
  ...(process.platform === 'win32' ? ['--disable-zitter-updates'] : []),
  '--no-proxy-server', '--remote-debugging-port=0',
  work.href,
], {stdio: ['ignore', 'ignore', 'pipe'], env: browserEnvironment});
let stderr = '';
child.stderr.setEncoding('utf8');
child.stderr.on('data', data => { stderr = (stderr + data).slice(-4000); });
const exited = new Promise((resolve, reject) => {
  child.once('exit', resolve);
  child.once('error', reject);
});

function waitForExit(timeout) {
  return Promise.race([
    exited.then(code => ({code})),
    sleep(timeout, null, {ref: false}),
  ]);
}

async function until(callback, label) {
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline && child.exitCode === null) {
    const result = await callback();
    if (result)
      return result;
    await sleep(100);
  }
  throw new Error(`Legacy browser did not provide ${label}\n${stderr}`);
}

let socket;
let sequence = 0;
const requests = new Map();
function call(method, params = {}, sessionId) {
  const id = ++sequence;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      requests.delete(id);
      reject(new Error(`Legacy browser CDP timeout: ${method}`));
    }, 5000);
    requests.set(id, {resolve, reject, timer});
    socket.send(JSON.stringify({id, method, params, sessionId}));
  });
}

try {
  const endpoint = await until(async () => {
    try {
      const [port, path] = (await readFile(join(profilePath, 'DevToolsActivePort'), 'utf8')).split(/\r?\n/);
      return /^\d+$/.test(port) && path?.startsWith('/devtools/browser/') &&
        `ws://127.0.0.1:${port}${path}`;
    } catch (error) {
      if (error.code !== 'ENOENT')
        throw error;
      return false;
    }
  }, 'a local DevTools endpoint');
  socket = new WebSocket(endpoint);
  await new Promise((resolve, reject) => {
    socket.addEventListener('open', resolve, {once: true});
    socket.addEventListener('error', reject, {once: true});
  });
  socket.addEventListener('message', event => {
    const {id, result, error} = JSON.parse(event.data);
    const request = requests.get(id);
    if (!request)
      return;
    requests.delete(id);
    clearTimeout(request.timer);
    if (error)
      request.reject(new Error(error.message));
    else
      request.resolve(result);
  });

  const target = await until(async () => {
    const {targetInfos} = await call('Target.getTargets');
    return targetInfos.find(info => info.type === 'page' && info.url === work.href);
  }, 'the profile fixture tab');
  const {sessionId} = await call('Target.attachToTarget', {targetId: target.targetId, flatten: true});
  if (storageProbe) {
    const expected = action.endsWith('-work') ? 'work' : 'personal';
    const name = 'profile_probe';
    await until(async () => {
      const {result} = await call('Runtime.evaluate', {
        expression: 'document.readyState === "complete" && location.href',
        returnByValue: true,
      }, sessionId);
      return result.value === work.href;
    }, 'the loaded legacy storage fixture');
    if (action.startsWith('seed-')) {
      const {success} = await call('Network.setCookie', {
        name, value: expected, url: work.href, path: '/', httpOnly: true,
        sameSite: 'Strict', expires: Date.now() / 1000 + 1800,
      }, sessionId);
      if (!success)
        throw new Error('Could not seed the legacy storage cookie');
      const {exceptionDetails} = await call('Runtime.evaluate', {
        expression: `localStorage.setItem(${JSON.stringify(name)}, ${JSON.stringify(expected)})`,
      }, sessionId);
      if (exceptionDetails)
        throw new Error('Could not seed legacy localStorage');
    }
    await until(async () => {
      const {cookies} = await call('Network.getCookies', {urls: [work.href]}, sessionId);
      const {result} = await call('Runtime.evaluate', {
        expression: `localStorage.getItem(${JSON.stringify(name)})`, returnByValue: true,
      }, sessionId);
      return result.value === expected && cookies.some(cookie =>
        cookie.name === name && cookie.value === expected && cookie.httpOnly &&
        !cookie.session && cookie.sameSite === 'Strict');
    }, 'the expected legacy cookie and localStorage');
    console.log(`Legacy storage ${action} verified`);
  } else if (clipboardProbe) {
    const allowed = action.endsWith('-allow');
    if (action.startsWith('seed-')) {
      const navigation = await call('Page.navigate', {url: 'chrome://settings/content/clipboard'}, sessionId);
      if (navigation.errorText)
        throw new Error(`Could not open legacy Clipboard settings: ${navigation.errorText}`);
      await until(async () => {
        const {result} = await call('Runtime.evaluate', {
          expression: "location.host === 'settings' && !!chrome.settingsPrivate?.getPref",
          returnByValue: true,
        }, sessionId);
        return result.value;
      }, 'the Clipboard settings API');
      const permission = allowed ? 'allow' : 'block';
      await call('Runtime.evaluate', {
        expression: `chrome.send('setCategoryPermissionForPattern',
          ${JSON.stringify([work.origin, '', 'clipboard', permission, false])})`,
      }, sessionId);
      const opened = await call('Page.navigate', {url: work.href}, sessionId);
      if (opened.errorText)
        throw new Error(`Could not reopen the legacy Clipboard fixture: ${opened.errorText}`);
    }
    await until(async () => {
      const {result} = await call('Runtime.evaluate', {
        expression: `(async () => location.origin === ${JSON.stringify(work.origin)} &&
          (await navigator.permissions.query({name: 'clipboard-read'})).state ===
              ${JSON.stringify(allowed ? 'granted' : 'denied')})()`,
        awaitPromise: true, returnByValue: true,
      }, sessionId);
      return result.value;
    }, 'the expected legacy Clipboard permission');
    console.log(`Legacy Clipboard ${action} verified`);
  } else if (settingsProbe) {
    const navigation = await call('Page.navigate', {url: 'chrome://settings/'}, sessionId);
    if (navigation.errorText)
      throw new Error(`Could not open legacy settings: ${navigation.errorText}`);
    await until(async () => {
      const {result} = await call('Runtime.evaluate', {
        expression: "location.host === 'settings' && !!chrome.settingsPrivate?.getPref",
        returnByValue: true,
      }, sessionId);
      return result.value;
    }, 'the settings API');
    const pref = "'credentials_enable_service'";
    const {result} = await call('Runtime.evaluate', {
      expression: `(async () => {
        if (${action === 'disable-password-saving'} &&
            !await new Promise(resolve =>
              chrome.settingsPrivate.setPref(${pref}, false, '', resolve))) return false;
        const saved = await new Promise(resolve =>
          chrome.settingsPrivate.getPref(${pref}, resolve));
        return saved?.value === false;
      })()`,
      awaitPromise: true, returnByValue: true,
    }, sessionId);
    if (!result.value)
      throw new Error('Legacy password saving preference was not disabled');
    console.log(`Legacy password setting ${action} verified`);
  } else if (passwordProbe) {
    const suffix = action.endsWith('-personal') ? '-personal' : '';
    const username = 'zitter-migration-user' + suffix;
    const password = 'zitter-migration-password' + suffix;
    const managerUrl = 'chrome://password-manager/passwords';
    const navigation = await call('Page.navigate', {url: managerUrl}, sessionId);
    if (navigation.errorText)
      throw new Error(`Could not open the legacy password manager: ${navigation.errorText}`);
    await until(async () => {
      const {result} = await call('Runtime.evaluate', {
        expression: "location.host === 'password-manager' && !!chrome.passwordsPrivate",
        returnByValue: true,
      }, sessionId);
      return result.value;
    }, 'the password manager API');

    if (action.startsWith('seed-password')) {
      const seeded = await call('Runtime.evaluate', {
        expression: `chrome.passwordsPrivate.addPassword({
          url: ${JSON.stringify(work.origin + '/')},
          username: ${JSON.stringify(username)},
          password: ${JSON.stringify(password)},
          note: '',
          useAccountStore: false,
        })`,
        awaitPromise: true, userGesture: true,
      }, sessionId);
      if (seeded.exceptionDetails)
        throw new Error('Legacy password manager could not save the synthetic credential');
    }
    await until(async () => {
      const {result} = await call('Runtime.evaluate', {
        expression: `chrome.passwordsPrivate.getSavedPasswordList().then(
          entries => entries.some(entry => entry.username === ${JSON.stringify(username)}))`,
        awaitPromise: true, returnByValue: true,
      }, sessionId);
      return result.value;
    }, 'the saved credential');
    console.log(`Legacy password ${action} verified`);
  } else {
    const extensionUrl = `chrome-extension://${extensionId}/probe.html`;
    const navigation = await call('Page.navigate', {url: extensionUrl}, sessionId);
    if (navigation.errorText)
      throw new Error(`Could not open the legacy extension page: ${navigation.errorText}\n${stderr}`);
    await until(async () => {
      const {result} = await call('Runtime.evaluate', {
        expression: `location.href === ${JSON.stringify(extensionUrl)} && chrome.runtime?.id === ${JSON.stringify(extensionId)}`,
        returnByValue: true,
      }, sessionId);
      return result.value;
    }, 'the extension API');
    const noLocalStorage = storageMode === 'without-local-storage';
    const {result, exceptionDetails} = await call('Runtime.evaluate', {
      expression: `(async () => {
        ${action !== 'read' && !noLocalStorage ? `await chrome.storage.local.set({counter: ${counter}, legacyMarker: 'chrome-profile'});` : ''}
        ${alarmTime ? `
          const when = ${JSON.stringify(Number(alarmTime))};
          if (${action !== 'read'}) await chrome.alarms.create('legacy-persistent', {when});
          const alarm = await chrome.alarms.get('legacy-persistent');
          if (alarm?.scheduledTime !== when) throw Error('Legacy alarm time changed');
        ` : ''}
        return ${noLocalStorage ? 'true' : "chrome.storage.local.get(['counter', 'legacyMarker'])"};
      })()`,
      awaitPromise: true, returnByValue: true,
    }, sessionId);
    if (exceptionDetails || (noLocalStorage ? result.value !== true :
        result.value?.counter !== counter || result.value?.legacyMarker !== 'chrome-profile'))
      throw new Error(`Unexpected legacy extension data: ${exceptionDetails?.exception?.description ?? JSON.stringify(result.value)}`);
    if (action === 'seed-disabled') {
      const settings = await call('Page.navigate', {url: 'chrome://extensions/'}, sessionId);
      if (settings.errorText)
        throw new Error(`Could not open legacy extension settings: ${settings.errorText}`);
      await until(async () => {
        const {result} = await call('Runtime.evaluate', {
          expression: "location.href === 'chrome://extensions/' && !!chrome.management",
          returnByValue: true,
        }, sessionId);
        return result.value;
      }, 'extension management API');
      const disabled = await call('Runtime.evaluate', {
        expression: `(async () => {
          const id = ${JSON.stringify(extensionId)};
          await chrome.management.setEnabled(id, false);
          return !(await chrome.management.get(id)).enabled;
        })()`,
        awaitPromise: true, returnByValue: true, userGesture: true,
      }, sessionId);
      if (disabled.exceptionDetails || disabled.result.value !== true)
        throw new Error(`Legacy extension could not be disabled: ${JSON.stringify(disabled)}`);
    }
    console.log(`Legacy extension ${noLocalStorage ? 'alarm fixture' : 'storage'} ${action} verified`);
  }
  await Promise.race([call('Browser.close'), exited]);
  const closed = await waitForExit(8000);
  if (!closed || closed.code !== 0)
    throw new Error(`Legacy browser did not close cleanly\n${stderr}`);
} finally {
  socket?.close();
  for (const request of requests.values())
    clearTimeout(request.timer);
  if (child.pid && child.exitCode === null && child.signalCode === null) {
    child.kill('SIGTERM');
    if (!await waitForExit(3000)) {
      child.kill('SIGKILL');
      await exited;
    }
  }
}
