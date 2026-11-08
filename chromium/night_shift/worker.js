// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

import "./site_policy.js";
import {compatibility} from "./vendor/compatibility.js";

const {repair, siteKey, siteSettings} = globalThis.NightShiftPolicy;
const ENABLED = "enabled";
const MAX_STYLESHEET_BYTES = 1024 * 1024;
const SCRIPTS = [
    {id: "night-shift-proxy", world: "MAIN", js: ["vendor/proxy.js"]},
    {id: "night-shift-theme", world: "ISOLATED", js: [
        "site_policy.js", "vendor/darkreader.js", "vendor/detector.js", "theme.js",
    ]},
];
const SCRIPT_IDS = SCRIPTS.map(script => script.id);
const extensionOrigin = chrome.runtime.getURL("");
let pending = Promise.resolve();
let compatibilityIndex;

function enqueue(operation) {
    const task = pending.then(operation);
    pending = task.catch(() => {});
    return task;
}

async function configureScripts(enabled) {
    const registered = await chrome.scripting.getRegisteredContentScripts({ids: SCRIPT_IDS});
    if (!enabled) {
        if (registered.length) {
            await chrome.scripting.unregisterContentScripts({ids: registered.map(script => script.id)});
        }
        return;
    }

    const existing = new Set(registered.map(script => script.id));
    const missing = SCRIPTS.filter(script => !existing.has(script.id));
    if (missing.length) {
        await chrome.scripting.registerContentScripts(missing.map(script => ({
            ...script,
            matches: ["http://*/*", "https://*/*"],
            allFrames: true,
            matchOriginAsFallback: true,
            runAt: "document_start",
            persistAcrossSessions: true,
        })));
    }
}

async function applyToOpenTabs() {
    const tabs = await chrome.tabs.query({url: ["http://*/*", "https://*/*"]});
    await Promise.allSettled(tabs.map(async tab => {
        if (tab.id == null || tab.discarded) {
            return;
        }
        const frames = await chrome.scripting.executeScript({
            target: {tabId: tab.id, allFrames: true},
            func: () => globalThis.zitterNightShiftActive === true,
            injectImmediately: true,
        });
        const frameIds = frames.filter(frame => !frame.result).map(frame => frame.frameId);
        if (!frameIds.length) {
            return;
        }
        for (const {world, js} of SCRIPTS) {
            await chrome.scripting.executeScript({
                target: {tabId: tab.id, frameIds},
                world,
                files: js,
                injectImmediately: true,
            });
        }
    }));
}

async function setEnabled(enabled) {
    if (enabled) {
        await configureScripts(true);
        try {
            await chrome.storage.local.set({[ENABLED]: true});
        } catch (error) {
            await configureScripts(false);
            throw error;
        }
        await applyToOpenTabs();
    } else {
        await chrome.storage.local.set({[ENABLED]: false});
        await configureScripts(false);
    }
    return {enabled};
}

async function restoreRegistration() {
    const values = await chrome.storage.local.get(ENABLED);
    await configureScripts(values[ENABLED] === true);
}

chrome.runtime.onStartup.addListener(() => enqueue(restoreRegistration).catch(console.error));
chrome.runtime.onInstalled.addListener(() => enqueue(restoreRegistration).catch(console.error));

function getCompatibility() {
    if (!compatibilityIndex) {
        const text = name => fetch(chrome.runtime.getURL("vendor/" + name)).then(response => {
            if (!response.ok) {
                throw new Error("Could not read Night Shift compatibility data");
            }
            return response.text();
        });
        compatibilityIndex = Promise.all([text("dynamic-theme-fixes.config"), text("dark-sites.config")])
            .then(([fixes, darkSites]) => compatibility(fixes, darkSites))
            .catch(error => {
                compatibilityIndex = null;
                throw error;
            });
    }
    return compatibilityIndex;
}

async function getDocumentState(sender) {
    const key = siteKey(sender.tab?.url);
    if (!key) {
        return {enabled: false, globallyDisabled: true};
    }
    const values = await chrome.storage.local.get([ENABLED, key]);
    const settings = siteSettings(values[key]);
    if (values[ENABLED] !== true) {
        return {enabled: false, globallyDisabled: true};
    }
    if (settings.mode === "off") {
        return {enabled: false, key};
    }

    const frameUrl = siteKey(sender.url) ? sender.url : sender.tab.url;
    const lookup = await getCompatibility();
    const {fixes, dark} = lookup(frameUrl);
    return {enabled: true, key, settings, fixes, dark};
}

async function linkedStylesheet(message, sender) {
    if (typeof message.url !== "string" || !siteKey(sender.url) || !siteKey(message.url)) {
        throw new Error("Invalid stylesheet URL");
    }
    const url = new URL(message.url);
    const documentUrl = new URL(sender.url);
    const crossOrigin = url.origin !== documentUrl.origin;
    const safeScheme = url.protocol === "https:" ||
        (url.protocol === documentUrl.protocol && url.hostname === documentUrl.hostname);
    if (!crossOrigin || !safeScheme || url.username || url.password) {
        throw new Error("Unavailable stylesheet origin");
    }
    const key = siteKey(sender.tab.url);
    if (!key) {
        throw new Error("Unavailable top-level site");
    }
    const values = await chrome.storage.local.get([ENABLED, key]);
    if (values[ENABLED] !== true || siteSettings(values[key]).mode === "off") {
        throw new Error("Night Shift is disabled");
    }
    const response = await fetch(url, {
        credentials: "omit",
        redirect: "error",
        referrerPolicy: "no-referrer",
        signal: AbortSignal.timeout(5000),
    });
    const mime = response.headers.get("content-type")?.split(";")[0].trim().toLowerCase();
    if (!response.ok || mime !== "text/css" || !response.body ||
        Number(response.headers.get("content-length")) > MAX_STYLESHEET_BYTES) {
        await response.body?.cancel();
        throw new Error("Unsupported stylesheet response");
    }
    const reader = response.body.getReader();
    const decoder = new TextDecoder();
    let bytes = 0;
    let css = "";
    while (true) {
        const {done, value} = await reader.read();
        if (done) {
            return css + decoder.decode();
        }
        bytes += value.byteLength;
        if (bytes > MAX_STYLESHEET_BYTES) {
            await reader.cancel();
            throw new Error("Stylesheet exceeds the Night Shift limit");
        }
        css += decoder.decode(value, {stream: true});
    }
}

async function tabSettings(tabId) {
    if (!Number.isInteger(tabId) || tabId < 0) {
        throw new Error("No active tab");
    }
    const tab = await chrome.tabs.get(tabId);
    const key = siteKey(tab.url);
    const values = await chrome.storage.local.get(key ? [ENABLED, key] : [ENABLED]);
    return {enabled: values[ENABLED] === true, key, url: tab.url, settings: siteSettings(values[key])};
}

async function writeSite(message) {
    const current = await tabSettings(message.tabId);
    if (!current.enabled || !current.key || message.key !== current.key) {
        throw new Error("The tab's site or extension state changed");
    }
    const next = siteSettings({...current.settings, ...message.settings});
    await chrome.storage.local.set({[current.key]: next});
    return next;
}

async function tuneSite(message) {
    const {mode, brightness, contrast} = message.settings ?? {};
    const settings = {};
    if (mode !== undefined) settings.mode = mode;
    if (brightness !== undefined) settings.brightness = brightness;
    if (contrast !== undefined) settings.contrast = contrast;
    return writeSite({...message, settings});
}

function draftKey(tabId) {
    return "night-shift-draft:" + tabId;
}

async function draftFor(tabId) {
    const current = await tabSettings(tabId);
    const key = draftKey(tabId);
    const data = (await chrome.storage.session.get(key))[key];
    const proposal = repair(data?.proposal);
    if (!proposal || !current.enabled || current.settings.mode === "off" ||
        data.url !== current.url || typeof data.documentId !== "string") {
        return null;
    }
    try {
        const ready = await chrome.tabs.sendMessage(tabId, {type: "night-shift-review-ready"}, {
            documentId: data.documentId,
        });
        return ready ? {documentId: data.documentId, key: current.key, proposal} : null;
    } catch {
        return null;
    }
}

async function finishDraft(message) {
    const draft = await draftFor(message.tabId);
    const expected = repair(message.proposal);
    if (!draft || message.documentId !== draft.documentId || !expected ||
        JSON.stringify(expected) !== JSON.stringify(draft.proposal)) {
        throw new Error("The Night Shift draft changed");
    }
    if (message.type === "save-review") {
        const current = await tabSettings(message.tabId);
        await writeSite({tabId: message.tabId, key: draft.key, settings: {
            repair: draft.proposal, previousRepair: current.settings.repair,
        }});
    }
    await chrome.storage.session.remove(draftKey(message.tabId));
    return true;
}

async function undoRepair(message) {
    const current = await tabSettings(message.tabId);
    if (!current.settings.repair) return current.settings;
    return writeSite({...message, settings: {
        repair: current.settings.previousRepair, previousRepair: null,
    }});
}

async function dispatch(message, sender) {
    if (sender.id !== chrome.runtime.id) {
        throw new Error("Unknown Night Shift sender");
    }
    if (message?.type === "document-state" && sender.tab) {
        return getDocumentState(sender);
    }
    if (message?.type === "linked-stylesheet" && sender.tab) {
        return linkedStylesheet(message, sender);
    }
    if (!sender.url?.startsWith(extensionOrigin)) {
        throw new Error("Night Shift settings require an extension page");
    }
    switch (message?.type) {
        case "tab-settings":
            return tabSettings(message.tabId);
        case "set-site":
            return enqueue(() => tuneSite(message));
        case "undo-review":
            return enqueue(() => undoRepair(message));
        case "get-review":
            return draftFor(message.tabId);
        case "save-review":
        case "discard-review":
            return enqueue(() => finishDraft(message));
        case "set-global":
            if (typeof message.enabled !== "boolean") {
                throw new Error("Invalid extension state");
            }
            return enqueue(() => setEnabled(message.enabled));
        default:
            throw new Error("Unknown Night Shift request");
    }
}

chrome.runtime.onMessage.addListener((message, sender, respond) => {
    dispatch(message, sender).then(
        value => respond({ok: true, value}),
        error => respond({ok: false, error: error.message}),
    );
    return true;
});
