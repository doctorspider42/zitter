// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

import "./site_policy.js";

const strings = {
    en: {
        subtitle: "Dynamic dark pages", settings: "Extension settings",
        siteToggle: "Dark mode for this website", customize: "Customize this site",
        brightness: "Brightness", contrast: "Contrast", reset: "Reset tuning",
        force: "Also apply over the site's own dark mode",
        globalOff: "Disabled in this profile", unsupported: "Unavailable on this page",
        siteOff: "Off for this site", siteOn: "Dark mode is on", automatic: "Automatic",
        failed: "Night Shift could not update this site.",
        repair: "Fix this site", draft: "Assistant suggestion · applies only to this site",
        before: "Before", after: "With fix", discard: "Discard", apply: "Save for this site",
        undo: "Undo site fix", stale: "The page or suggestion changed. Open Night Shift again.",
    },
    pl: {
        subtitle: "Dynamiczny tryb ciemny", settings: "Ustawienia rozszerzenia",
        siteToggle: "Tryb ciemny dla tej witryny", customize: "Dostosuj tę witrynę",
        brightness: "Jasność", contrast: "Kontrast", reset: "Przywróć ustawienia",
        force: "Stosuj także na stronach z własnym trybem ciemnym",
        globalOff: "Wyłączone w tym profilu", unsupported: "Niedostępne na tej stronie",
        siteOff: "Wyłączony dla tej witryny", siteOn: "Tryb ciemny włączony",
        automatic: "Tryb automatyczny", failed: "Nie udało się zaktualizować ustawień witryny.",
        repair: "Popraw dla tej strony", draft: "Propozycja asystenta · tylko dla tej witryny",
        before: "Przed", after: "Z poprawką", discard: "Odrzuć", apply: "Zapisz dla tej witryny",
        undo: "Cofnij poprawkę", stale: "Strona lub propozycja się zmieniła. Otwórz Night Shift ponownie.",
    },
};
const params = new URLSearchParams(location.search);
const language = params.get("language") === "pl" || (!params.has("language") && navigator.language.startsWith("pl")) ? "pl" : "en";
const copy = strings[language];
document.documentElement.lang = language;
const appearance = params.get("appearance");
if (appearance === "dark" || appearance === "light") {
    document.documentElement.dataset.appearance = appearance;
}
const settingsLink = document.querySelector(".gear");
const settingsUrl = new URL(settingsLink.href);
settingsUrl.searchParams.set("language", language);
if (appearance === "dark" || appearance === "light") {
    settingsUrl.searchParams.set("appearance", appearance);
}
settingsLink.href = settingsUrl;
for (const element of document.querySelectorAll("[data-text]")) {
    element.textContent = copy[element.dataset.text];
}
for (const element of document.querySelectorAll("[data-label]")) {
    const label = copy[element.dataset.label];
    element.setAttribute("aria-label", label);
    if (element.hasAttribute("title")) {
        element.title = label;
    }
}

const get = id => document.getElementById(id);
const toggle = get("site-toggle");
const tools = get("site-tools");
const sliders = [get("brightness"), get("contrast")];
let tabId;
let state;
let timer;
let update = {};
let draft;
let previewPort;
let reviewing = false;

async function request(message) {
    const response = await chrome.runtime.sendMessage(message);
    if (!response?.ok) {
        throw new Error(response?.error);
    }
    return response.value;
}

function render() {
    const {enabled, key, settings} = state;
    get("site-name").textContent = key?.slice("site:".length) || "Night Shift";
    get("site-state").textContent = !enabled ? copy.globalOff : !key ? copy.unsupported :
        settings.mode === "off" ? copy.siteOff : settings.mode === "on" ? copy.siteOn : copy.automatic;
    toggle.checked = Boolean(enabled && key && settings.mode !== "off");
    toggle.disabled = !enabled || !key;
    tools.hidden = !toggle.checked;
    get("force").checked = settings.mode === "on";
    get("repair").hidden = params.get("assistant") !== "1" || Boolean(draft);
    get("review").hidden = !draft;
    get("undo").hidden = !settings.repair || Boolean(draft);
    for (const slider of sliders) {
        slider.value = settings[slider.id];
        slider.closest("label").querySelector("output").value = slider.value + "%";
    }
}

function compare(withFix) {
    if (!draft) {
        return;
    }
    previewPort ??= chrome.tabs.connect(tabId, {
        name: "night-shift-review", documentId: draft.documentId,
    });
    previewPort.postMessage({proposal: withFix ? draft.proposal : null});
    get("before").setAttribute("aria-pressed", String(!withFix));
    get("after").setAttribute("aria-pressed", String(withFix));
}

async function refreshDraft() {
    if (reviewing) {
        return;
    }
    const next = await request({type: "get-review", tabId});
    if (JSON.stringify(next) === JSON.stringify(draft)) {
        return;
    }
    previewPort?.disconnect();
    previewPort = null;
    draft = next;
    if (draft) {
        get("review-title").textContent = draft.proposal.label;
        compare(true);
    }
    render();
}

async function finishReview(type) {
    if (!draft || reviewing) {
        return;
    }
    reviewing = true;
    get("apply").disabled = true;
    get("discard").disabled = true;
    try {
        await request({type, tabId, documentId: draft.documentId, proposal: draft.proposal});
        state = await request({type: "tab-settings", tabId});
    } catch {
        get("message").textContent = copy.stale;
    } finally {
        reviewing = false;
        get("apply").disabled = false;
        get("discard").disabled = false;
        await refreshDraft();
    }
}

function save(change, debounce = false) {
    Object.assign(state.settings, change);
    if (change.mode === "on" || change.mode === "auto") {
        state.settings.force = change.mode === "on";
    }
    Object.assign(update, change);
    render();
    clearTimeout(timer);
    if (debounce) {
        timer = setTimeout(flush, 65);
    } else {
        flush();
    }
}

function flush() {
    clearTimeout(timer);
    if (!Object.keys(update).length) {
        return;
    }
    const settings = update;
    update = {};
    get("message").textContent = "";
    request({type: "set-site", tabId, key: state.key, settings}).catch(async () => {
        get("message").textContent = copy.failed;
        try {
            state = await request({type: "tab-settings", tabId});
            render();
        } catch {
            toggle.disabled = true;
            tools.hidden = true;
        }
    });
}

toggle.addEventListener("change", () => save({
    mode: !toggle.checked ? "off" : state.settings.force ? "on" : "auto",
}));
get("force").addEventListener("change", event => save({mode: event.target.checked ? "on" : "auto"}));
for (const slider of sliders) {
    slider.addEventListener("input", () => save({[slider.id]: Number(slider.value)}, true));
    slider.addEventListener("change", flush);
}
// Back to the app-wide look from the Night Shift settings.
get("reset").addEventListener("click", () => {
    const look = globalThis.NightShiftPolicy.defaults(state.defaults);
    save({brightness: null, contrast: null, mode: "auto"});
    Object.assign(state.settings, {brightness: look.brightness, contrast: look.contrast, force: look.force});
    render();
});
get("before").addEventListener("click", () => compare(false));
get("after").addEventListener("click", () => compare(true));
get("apply").addEventListener("click", () => finishReview("save-review"));
get("discard").addEventListener("click", () => finishReview("discard-review"));
get("undo").addEventListener("click", async () => {
    try {
        state.settings = await request({type: "undo-review", tabId, key: state.key});
        render();
    } catch {
        get("message").textContent = copy.stale;
    }
});
chrome.storage.onChanged.addListener((changes, area) => {
    if (state && area === "session" && changes["night-shift-draft:" + tabId]) {
        refreshDraft().catch(() => { get("message").textContent = copy.stale; });
    }
});

// The first state comes straight from storage: asking the worker would also
// wait for it to start.
async function localSettings(id) {
    const {siteKey, siteSettings} = globalThis.NightShiftPolicy;
    const tab = await chrome.tabs.get(id);
    const key = siteKey(tab.url);
    const values = await chrome.storage.local.get(key ? ["enabled", "defaults", key] : ["enabled", "defaults"]);
    return {
        enabled: values.enabled === true, key, url: tab.url,
        settings: siteSettings(values[key], values.defaults), stored: values[key] ?? {},
        defaults: values.defaults,
    };
}

try {
    tabId = params.has("tab") ? Number(params.get("tab")) :
        (await chrome.tabs.query({active: true, currentWindow: true}))[0]?.id;
    state = await localSettings(tabId);
    render();
    refreshDraft().catch(() => { get("message").textContent = copy.stale; });
} catch {
    get("message").textContent = copy.failed;
} finally {
    // Until now the page has no size, so Zitter keeps its card hidden.
    document.body.classList.add("ready");
}
