// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

import "./site_policy.js";

const {defaults, siteSettings} = globalThis.NightShiftPolicy;
const strings = {
    en: {
        subtitle: "Dark pages where there are none", enable: "Turn on Night Shift",
        where: "Where it runs", whereHint: "One setting for all of Zitter. No need to turn it on in every profile.",
        scope: "Profiles", allProfiles: "All profiles", someProfiles: "Selected",
        allNote: "New profiles join automatically.", someNote: "Only the checked profiles.",
        sites: "Sites with their own settings", sitesEmpty: "Choose Night Shift on a page to tune that site.",
        sitesHint: "In this profile.", look: "Default look", lookHint: "For every site without its own settings.",
        repairTitle: "Assistant proposal", repairBefore: "Before", repairPreview: "Preview",
        repairHint: "The preview appears in the site tab.", repairDiscard: "Discard", repairSave: "Save for this site", repairUndo: "Undo last repair",
        modeDefault: "Default", modeDark: "Dark", modeLight: "Light",
        automatic: "Automatic", unavailable: "Unavailable on this page",
        fixSite: "Fix this site", openSettings: "Night Shift settings", resetTuning: "Reset tuning",
        siteLook: "Site appearance", brightness: "Brightness", contrast: "Contrast", sepia: "Sepia", grayscale: "Grayscale",
        force: "Also on sites with their own dark mode", forceHint: "By default a site's own dark theme stays",
        reset: "Reset to defaults", instant: "Changes apply right away", remove: "Remove the site's settings",
        off: "off", always: "always", fix: "AI fix", tuned: "brightness {}%", tunedContrast: "contrast {}%",
        current: "this profile", spaces: n => n === 1 ? "1 space" : n + " spaces",
        failed: "Could not change Night Shift. Try again.", loadFailed: "Could not load Night Shift settings.",
        single: "Open these settings from the Night Shift button in Zitter to choose profiles and the default look.",
    },
    pl: {
        subtitle: "Ciemne strony tam, gdzie ich nie ma", enable: "Włącz Night Shift",
        where: "Gdzie działa", whereHint: "Jedno ustawienie dla całego Zittera — nie trzeba włączać w każdym profilu osobno.",
        scope: "Profile", allProfiles: "Wszystkie profile", someProfiles: "Wybrane",
        allNote: "Nowe profile dołączają automatycznie.", someNote: "Tylko zaznaczone profile.",
        sites: "Witryny z własnymi ustawieniami", sitesEmpty: "Wybierz Night Shift na stronie, aby dostroić witrynę.",
        sitesHint: "W tym profilu.", look: "Domyślny wygląd", lookHint: "Dla każdej strony bez własnych ustawień.",
        repairTitle: "Propozycja Asystenta", repairBefore: "Przed", repairPreview: "Podgląd",
        repairHint: "Podgląd pojawi się w karcie witryny.", repairDiscard: "Odrzuć", repairSave: "Zapisz dla tej witryny", repairUndo: "Cofnij ostatnią poprawkę",
        modeDefault: "Domyślnie", modeDark: "Ciemna", modeLight: "Jasna",
        automatic: "Tryb automatyczny", unavailable: "Niedostępne na tej stronie",
        fixSite: "Popraw dla tej strony", openSettings: "Ustawienia Night Shift", resetTuning: "Przywróć ustawienia",
        siteLook: "Wygląd witryny", brightness: "Jasność", contrast: "Kontrast", sepia: "Sepia", grayscale: "Skala szarości",
        force: "Stosuj też na stronach z trybem ciemnym", forceHint: "Domyślnie zostaje własny motyw strony",
        reset: "Przywróć domyślne", instant: "Zmiany działają od razu", remove: "Usuń ustawienia witryny",
        off: "wyłączony", always: "zawsze", fix: "poprawka AI", tuned: "jasność {}%", tunedContrast: "kontrast {}%",
        current: "ten profil",
        spaces: n => n === 1 ? "1 przestrzeń" :
            [2, 3, 4].includes(n % 10) && ![12, 13, 14].includes(n % 100) ? n + " przestrzenie" : n + " przestrzeni",
        failed: "Nie udało się zmienić ustawienia. Spróbuj ponownie.", loadFailed: "Nie udało się wczytać ustawień Night Shift.",
        single: "Otwórz te ustawienia przyciskiem Night Shift w Zitterze, aby wybrać profile i domyślny wygląd.",
    },
};
const COLORS = ["#9a92ff", "#5fe0b8", "#ff8a66", "#5cc8ff", "#ffc35c", "#ff7eb6"];
const SLIDERS = ["brightness", "contrast", "sepia", "grayscale"];

const params = new URLSearchParams(location.search);
const quickPanel = params.get("presentation") === "panel";
if (quickPanel) {
    document.documentElement.dataset.presentation = "panel";
}
const language = params.get("language") === "pl" || (!params.has("language") && navigator.language.startsWith("pl")) ? "pl" : "en";
const copy = strings[language];
document.documentElement.lang = language;
const appearance = params.get("appearance");
if (appearance === "dark" || appearance === "light") {
    document.documentElement.dataset.appearance = appearance;
}
for (const element of document.querySelectorAll("[data-text]")) {
    element.textContent = copy[element.dataset.text];
}
for (const element of document.querySelectorAll("[data-label]")) {
    element.setAttribute("aria-label", copy[element.dataset.label]);
}

const get = id => document.getElementById(id);
const status = document.querySelector(".status");
const toggle = get("enabled");
let zitter = null;  // The profiles and saved configuration Zitter passed in.
try {
    zitter = params.has("state") ? JSON.parse(params.get("state")) : null;
} catch {
    zitter = null;
}
let config;
let revision = 0;
let timer;

function format(template, value) {
    return template.replace("{}", value);
}

// Zitter reads the change from this page's own URL fragment.
function apply() {
    clearTimeout(timer);
    const save = () => {
        const payload = {...config, profiles: [...config.profiles], revision: ++revision};
        history.replaceState(null, "", "#apply=" + encodeURIComponent(JSON.stringify(payload)));
    };
    if (quickPanel) save();
    else timer = setTimeout(save, 120);
}

function change(update) {
    Object.assign(config, update);
    render();
    apply();
}

function renderProfiles() {
    const list = get("profiles");
    list.replaceChildren();
    list.classList.toggle("locked", config.all_profiles);
    zitter.profiles.forEach((profile, index) => {
        const row = document.createElement("label");
        row.className = "profile";
        const dot = document.createElement("span");
        dot.className = "dot";
        dot.style.background = COLORS[index % COLORS.length];
        dot.textContent = (profile.name.trim()[0] ?? "?").toUpperCase();
        const name = document.createElement("span");
        name.className = "name";
        const title = document.createElement("span");
        title.textContent = profile.name;
        const detail = document.createElement("small");
        detail.textContent = [profile.current ? copy.current : "", copy.spaces(profile.spaces ?? 0)].filter(Boolean).join(" · ");
        name.append(title, detail);
        const check = document.createElement("input");
        check.type = "checkbox";
        check.checked = config.profile_overrides[profile.id] ?? (config.enabled && (config.all_profiles || config.profiles.includes(profile.id)));
        check.disabled = config.all_profiles || !config.enabled;
        check.setAttribute("aria-label", profile.name);
        check.addEventListener("change", () => {
            const profiles = new Set(config.profiles);
            if (check.checked) profiles.add(profile.id); else profiles.delete(profile.id);
            change({profiles: [...profiles]});
        });
        row.append(dot, name, check);
        list.append(row);
    });
}

function render() {
    const profile = zitter.profiles.find(item => item.current);
    toggle.checked = quickPanel && profile
        ? config.profile_overrides[profile.id] ?? (config.enabled && (config.all_profiles || config.profiles.includes(profile.id)))
        : config.enabled;
    for (const [id, all] of [["scope-all", true], ["scope-some", false]]) {
        get(id).setAttribute("aria-checked", String(config.all_profiles === all));
        get(id).disabled = !config.enabled;
    }
    get("scope-note").textContent = config.all_profiles ? copy.allNote : copy.someNote;
    renderProfiles();
    for (const id of SLIDERS) {
        get(id).value = config[id];
        get(id).closest("label").querySelector("output").value = config[id] + "%";
    }
    get("force").checked = config.force;
    get("preview").style.filter = `brightness(${config.brightness}%) contrast(${config.contrast}%) ` +
        `sepia(${config.sepia}%) grayscale(${config.grayscale}%)`;
}

async function openSiteRepair(key) {
    const tabId = Number(params.get("tab"));
    const documentId = params.get("document");
    if (!Number.isSafeInteger(tabId) || tabId <= 0 || !documentId) return;

    const request = async (type, extra = {}) => {
        const response = await chrome.runtime.sendMessage({type, key, tabId, documentId, ...extra});
        if (!response?.ok) throw new Error(response?.error);
        return response.value;
    };
    const panel = get("site-repair");
    const undo = get("site-repair-undo");
    const draft = await request("get-review");
    const site = await request("tab-settings");
    if (site.key !== key) return;
    undo.hidden = !site.settings.repair;
    const action = (id, callback) => {
        const control = get(id);
        control.addEventListener("click", async () => {
            if (panel.inert || undo.disabled) return;
            panel.inert = true;
            undo.disabled = true;
            try {
                await callback();
                status.textContent = "";
            } catch (error) {
                status.textContent = copy.failed;
                console.error(error);
            } finally {
                panel.inert = false;
                undo.disabled = false;
            }
        });
    };
    action("site-repair-undo", async () => {
        const updated = await request("undo-review");
        undo.hidden = !updated.repair;
    });
    if (!draft || draft.key !== key || draft.documentId !== documentId) return;

    let previewing = false;
    const preview = async show => {
        await request("preview-review", {show});
        previewing = show;
        get("site-repair-preview").setAttribute("aria-pressed", String(show));
        get("site-repair-before").setAttribute("aria-pressed", String(!show));
    };
    const finish = async type => {
        await request(type, {proposal: draft.proposal});
        previewing = false;
        panel.hidden = true;
        undo.hidden = !(await request("tab-settings")).settings.repair;
    };
    action("site-repair-before", () => preview(false));
    action("site-repair-preview", () => preview(true));
    action("site-repair-discard", () => finish("discard-review"));
    action("site-repair-save", () => finish("save-review"));
    window.addEventListener("pagehide", () => {
        if (previewing) request("preview-review", {show: false}).catch(() => {});
    });
    get("site-repair-label").textContent = draft.proposal.label;
    get("site-repair-before").setAttribute("aria-pressed", "true");
    get("site-repair-preview").setAttribute("aria-pressed", "false");
    panel.hidden = false;
}

async function openSiteTuning(key) {
    if (!zitter || !key?.startsWith("site:") || key.length <= 5 || key.length > 300)
        return;

    const fields = ["brightness", "contrast"];
    const inputs = fields.map(field => get("site-" + field));
    const values = await chrome.storage.local.get([key, "defaults"]);
    const ownDefaults = defaults(values.defaults);
    const saved = values[key] ?? {};
    const show = (input, value) => {
        input.value = value;
        document.querySelector(`output[for="${input.id}"]`).textContent = input.value + "%";
    };
    const reset = get("site-tuning-reset");
    let pending = Promise.resolve();
    const update = change => {
        pending = pending.then(async () => {
            const values = await chrome.storage.local.get(key);
            const record = {...values[key]};
            for (const [field, value] of Object.entries(change)) {
                if (value === null) delete record[field];
                else record[field] = value;
            }
            if (Object.keys(record).length) await chrome.storage.local.set({[key]: record});
            else await chrome.storage.local.remove(key);
            status.textContent = "";
        }).catch(error => {
            status.textContent = copy.failed;
            console.error(error);
        });
    };

    if (quickPanel) {
        const modes = [...get("quick-modes").querySelectorAll("button")];
        const force = get("quick-force");
        const select = mode => {
            for (const button of modes) {
                button.setAttribute("aria-pressed",
                    String(button.value === (mode === "on" ? "dark" : mode)));
            }
            force.checked = mode === "on" || (mode === "auto" && ownDefaults.force);
            force.disabled = mode === "off";
            get("quick-state").textContent = mode === "auto" ? copy.automatic
                : mode === "off" ? copy.modeLight : copy.modeDark;
        };
        select(siteSettings(saved).mode);
        for (const button of modes) {
            button.addEventListener("click", () => {
                const mode = button.value === "dark" && force.checked ? "on" : button.value;
                select(mode);
                update({mode, force: mode === "on"});
            });
        }
        force.addEventListener("change", () => {
            const mode = force.checked ? "on" : "dark";
            select(mode);
            update({mode, force: force.checked});
        });
        get("quick-host").textContent = key.slice(5);
        get("quick-modes").hidden = false;
        get("quick-force-row").hidden = false;
        reset.querySelector("[data-text]").textContent = copy.resetTuning;
    }

    fields.forEach((field, index) => {
        const input = inputs[index];
        show(input, saved[field] ?? ownDefaults[field]);
        input.addEventListener("input", () => show(input, input.value));
        input.addEventListener("change", () => update({[field]: Number(input.value)}));
    });
    reset.addEventListener("click", () => {
        fields.forEach((field, index) => show(inputs[index], ownDefaults[field]));
        update({brightness: null, contrast: null});
    });
    get("site-tuning-host").textContent = key.slice(5);
    get("site-tuning").hidden = false;
    await openSiteRepair(key);
}

async function renderSites() {
    const values = await chrome.storage.local.get(null);
    const list = get("sites");
    list.replaceChildren();
    const entries = Object.entries(values)
        .filter(([key]) => key.startsWith("site:"))
        .map(([key, value]) => [key, value ?? {}, siteSettings(value)])
        .filter(([, raw, settings]) => settings.mode !== "auto" || settings.repair ||
            Number.isInteger(raw.brightness) || Number.isInteger(raw.contrast))
        .sort(([a], [b]) => a.localeCompare(b));
    get("sites-empty").hidden = entries.length > 0;
    for (const [key, raw, settings] of entries) {
        const row = document.createElement("div");
        row.className = "site";
        const host = document.createElement("span");
        host.className = "host";
        host.textContent = key.slice("site:".length);
        const tag = document.createElement("span");
        tag.className = "tag";
        if (settings.mode === "off") {
            tag.classList.add("off");
            tag.textContent = copy.off;
        } else if (settings.repair) {
            tag.textContent = copy.fix;
        } else if (settings.mode === "on" || settings.mode === "dark") {
            tag.textContent = copy.always;
        } else if (Number.isInteger(raw.brightness)) {
            tag.textContent = format(copy.tuned, raw.brightness);
        } else {
            tag.textContent = format(copy.tunedContrast, raw.contrast);
        }
        const remove = document.createElement("button");
        remove.type = "button";
        remove.className = "remove";
        remove.setAttribute("aria-label", copy.remove);
        remove.title = copy.remove;
        remove.innerHTML = '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M4 4l8 8M12 4l-8 8"/></svg>';
        remove.addEventListener("click", async () => {
            await chrome.storage.local.remove(key);
            await renderSites();
        });
        row.append(host, tag, remove);
        list.append(row);
    }
}

// Outside Zitter's toolbar there is no profile list: only this profile's switch.
async function standalone() {
    document.body.classList.add("single");
    get("scope-note").textContent = "";
    status.textContent = copy.single;
    const read = async () => {
        toggle.checked = (await chrome.storage.local.get("enabled")).enabled === true;
    };
    toggle.addEventListener("change", async () => {
        toggle.disabled = true;
        try {
            const result = await chrome.runtime.sendMessage({type: "set-global", enabled: toggle.checked});
            if (!result?.ok) throw new Error(result?.error);
        } catch (error) {
            status.textContent = copy.failed;
            console.error(error);
        } finally {
            await read();
            toggle.disabled = false;
        }
    });
    await read();
    toggle.disabled = false;
}

async function managed() {
    const own = await chrome.storage.local.get(["enabled", "defaults"]);
    const look = defaults(own.defaults);
    const saved = zitter.config;
    const ids = zitter.profiles.map(profile => profile.id);
    config = {
        enabled: saved ? saved.enabled === true : own.enabled === true,
        all_profiles: saved ? saved.all_profiles !== false : true,
        profiles: (saved?.profiles ?? ids).filter(id => ids.includes(id)),
        profile_overrides: Object.fromEntries(Object.entries(saved?.profile_overrides ?? {})
            .filter(([id, enabled]) => ids.includes(id) && typeof enabled === "boolean")),
        brightness: look.brightness, contrast: look.contrast, sepia: look.sepia, grayscale: look.grayscale,
        force: look.force,
    };
    if (saved) {
        Object.assign(config, defaults(saved));
    }
    toggle.addEventListener("change", () => {
        const profile = zitter.profiles.find(item => item.current);
        if (quickPanel && profile) {
            change({profile_overrides: {...config.profile_overrides, [profile.id]: toggle.checked}});
        } else {
            change({enabled: toggle.checked, profile_overrides: {}});
        }
    });
    get("scope-all").addEventListener("click", () => change({all_profiles: true, profile_overrides: {}}));
    get("scope-some").addEventListener("click", () => change({all_profiles: false, profile_overrides: {}}));
    for (const id of SLIDERS) {
        get(id).addEventListener("input", () => change({[id]: Number(get(id).value)}));
    }
    get("force").addEventListener("change", () => change({force: get("force").checked}));
    get("reset").addEventListener("click", () => change({brightness: 100, contrast: 100, sepia: 0, grayscale: 0, force: false}));
    render();
    toggle.disabled = false;
}

chrome.storage.onChanged.addListener((changes, area) => {
    if (area === "local" && Object.keys(changes).some(key => key.startsWith("site:"))) {
        if (zitter) history.replaceState(null, "", "#site-changed=" + (++revision));
        renderSites().catch(console.error);
    }
});

try {
    if (quickPanel) {
        get("quick-settings").hidden = false;
        get("quick-settings").addEventListener("click", () => history.replaceState(null, "", "#settings"));
        get("quick-site").hidden = false;
        get("quick-modes").hidden = true;
        get("quick-state").textContent = copy.unavailable;
        if (params.get("repair") === "1") {
            get("quick-fix").hidden = false;
            get("quick-fix").addEventListener("click", () => history.replaceState(null, "", "#repair"));
        }
    }
    if (zitter && Array.isArray(zitter.profiles)) {
        await managed();
        await openSiteTuning(params.get("site"));
    } else {
        await standalone();
    }
    await renderSites();
} catch (error) {
    status.textContent = copy.loadFailed;
    console.error(error);
}
