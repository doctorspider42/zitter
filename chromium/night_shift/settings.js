// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

const toggle = document.querySelector("input");
const status = document.querySelector(".status");
const params = new URLSearchParams(location.search);
const polish = params.get("language") === "pl" || (!params.has("language") && navigator.language.startsWith("pl"));
const appearance = params.get("appearance");
if (appearance === "dark" || appearance === "light") {
    document.documentElement.dataset.appearance = appearance;
}
if (polish) {
    document.documentElement.lang = "pl";
    document.querySelector("#subtitle").textContent = "Dynamiczny tryb ciemny";
    document.querySelector("#enable").textContent = "Włącz Night Shift";
    document.querySelector("#profile").textContent = "Obowiązuje w tym profilu.";
}

async function read() {
    const {enabled} = await chrome.storage.local.get("enabled");
    toggle.checked = enabled === true;
}

toggle.addEventListener("change", async () => {
    toggle.disabled = true;
    status.textContent = "";
    try {
        const result = await chrome.runtime.sendMessage({type: "set-global", enabled: toggle.checked});
        if (!result?.ok) {
            throw new Error(result?.error);
        }
    } catch (error) {
        status.textContent = polish ? "Nie udało się zmienić ustawienia. Spróbuj ponownie." :
            "Could not change Night Shift. Try again.";
        console.error(error);
    } finally {
        await read();
        toggle.disabled = false;
    }
});

chrome.storage.onChanged.addListener((changes, area) => {
    if (area === "local" && changes.enabled) {
        toggle.checked = changes.enabled.newValue === true;
    }
});

try {
    await read();
    toggle.disabled = false;
} catch (error) {
    status.textContent = polish ? "Nie udało się wczytać ustawień Night Shift." :
        "Could not load Night Shift settings.";
    console.error(error);
}
