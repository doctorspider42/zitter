// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

(() => {
    "use strict";
    if (globalThis.zitterNightShiftActive) {
        return;
    }
    globalThis.zitterNightShiftActive = true;

    const engine = globalThis.DarkReader;
    const detector = globalThis.NightShiftDetector;
    const {repair, repairCss} = globalThis.NightShiftPolicy;
    const isMainFrame = window === window.top;
    const colorScheme = matchMedia("(prefers-color-scheme: dark)");
    let current = null;
    let darkDetected = false;
    let nativeObservation = null;
    let generation = 0;
    let appliedTheme = "";
    let disposed = false;
    let preview = null;
    let previewPort = null;
    const extensionOrigin = chrome.runtime.getURL("");

    const stylesheetOrigins = new Set();
    engine.Plugins = {fetch: async ({url, responseType, mimeType}) => {
        try {
            const response = await fetch(url, {
                credentials: "omit",
                redirect: "error",
                referrerPolicy: "no-referrer",
                signal: AbortSignal.timeout(8000),
            });
            if (!response.ok) throw new Error("Could not read theme resource");
            if (responseType !== "data-url") return response.text();
            const image = await response.blob();
            return await new Promise((resolve, reject) => {
                const reader = new FileReader();
                reader.onload = () => resolve(reader.result);
                reader.onerror = reject;
                reader.readAsDataURL(image);
            });
        } catch (error) {
            // Only a sheet the browser already loaded as a stylesheet may use
            // the extension's anonymous fetch. Images and arbitrary URLs stay
            // subject to the document's own CORS rules.
            if (mimeType !== "text/css" || !current?.enabled || ![...document.querySelectorAll('link[rel~="stylesheet" i]')]
                .some(link => link.href === url && link.sheet)) {
                throw error;
            }
            const origin = new URL(url).origin;
            if (!stylesheetOrigins.has(origin) && stylesheetOrigins.size >= 8) throw error;
            stylesheetOrigins.add(origin);
            const reply = await chrome.runtime.sendMessage({type: "linked-stylesheet", url});
            if (!reply?.ok || typeof reply.value !== "string") {
                throw error;
            }
            return reply.value;
        }
    }};

    function stopObservation() {
        nativeObservation?.disconnect();
        nativeObservation = null;
        detector.stopDarkThemeDetector();
        colorScheme.removeEventListener("change", observeNativeDark);
    }

    function render() {
        const useTheme = current?.enabled && (current.settings.force || !darkDetected);
        if (!useTheme) {
            if (engine.isEnabled()) {
                engine.disable();
            }
            appliedTheme = "";
            return;
        }
        const {brightness, contrast, sepia = 0, grayscale = 0, repair: savedRepair} = current.settings;
        const fixes = current.fixes ?? {};
        const css = [fixes.css, isMainFrame ? repairCss(preview ?? savedRepair) : ""].filter(Boolean).join("\n");
        const signature = JSON.stringify([brightness, contrast, sepia, grayscale, fixes, css]);
        if (signature !== appliedTheme) {
            engine.enable({brightness, contrast, sepia, grayscale, immediateModify: true}, {...fixes, css});
            appliedTheme = signature;
        }
    }

    function observeNativeDark() {
        if (!current?.enabled || current.settings.mode !== "auto" || current.dark || !document.documentElement) {
            return;
        }
        detector.runDarkThemeDetector(dark => {
            darkDetected = dark;
            render();
        }, []);
    }

    function startObservation() {
        const root = document.documentElement;
        if (!root) {
            document.addEventListener("DOMContentLoaded", () => {
                if (current?.enabled && current.settings.mode === "auto" && !current.dark) {
                    startObservation();
                }
            }, {once: true});
            return;
        }
        observeNativeDark();
        nativeObservation = new MutationObserver(observeNativeDark);
        const options = {attributes: true, attributeFilter: ["class", "data-theme", "color-scheme"]};
        nativeObservation.observe(root, options);
        if (document.body) {
            nativeObservation.observe(document.body, options);
        } else {
            document.addEventListener("DOMContentLoaded", () => {
                if (document.body) {
                    nativeObservation?.observe(document.body, options);
                }
            }, {once: true});
        }
        colorScheme.addEventListener("change", observeNativeDark);
    }

    function update(state) {
        if (state.globallyDisabled) {
            dispose();
            return;
        }
        const detect = state.enabled && state.settings.mode === "auto" && !state.settings.force && !state.dark;
        const previouslyDetected = Boolean(nativeObservation);
        current = state;
        if (!detect) {
            stopObservation();
            darkDetected = Boolean(state.dark);
        } else if (!previouslyDetected) {
            darkDetected = false;
            startObservation();
        }
        render();
    }

    async function refresh() {
        const request = ++generation;
        try {
            const response = await chrome.runtime.sendMessage({type: "document-state"});
            if (!disposed && request === generation) {
                update(response?.ok ? response.value : {enabled: false});
            }
        } catch {
            if (!disposed && request === generation) {
                update({enabled: false});
            }
        }
    }

    function onSettingsChanged(changes, area) {
        if (area !== "local" || (!changes.enabled && !changes.defaults && (!current?.key || !changes[current.key]))) {
            return;
        }
        if (changes.enabled?.newValue === false) {
            dispose();
        } else {
            refresh();
        }
    }

    function onPageShow(event) {
        if (event.persisted) {
            refresh();
        }
    }

    function onNavigation() {
        previewPort?.disconnect();
        previewPort = null;
        preview = null;
        render();
        refresh();
    }

    function isExtensionPage(sender) {
        return sender?.id === chrome.runtime.id && sender.url?.startsWith(extensionOrigin);
    }

    function onMessage(message, sender, respond) {
        if (isMainFrame && message?.type === "night-shift-review-ready" &&
            sender.id === chrome.runtime.id && !sender.tab) {
            respond(Boolean(current?.enabled && engine.isEnabled()));
        }
    }

    function onConnect(port) {
        if (!isMainFrame || port.name !== "night-shift-review" || !isExtensionPage(port.sender)) {
            return;
        }
        previewPort?.disconnect();
        previewPort = port;
        preview = null;
        render();
        port.onMessage.addListener(message => {
            if (previewPort !== port || !current?.enabled) {
                return;
            }
            preview = repair(message?.proposal);
            render();
        });
        port.onDisconnect.addListener(() => {
            if (previewPort === port) {
                previewPort = null;
                preview = null;
                render();
            }
        });
    }

    function dispose() {
        disposed = true;
        update({enabled: false});
        chrome.storage.onChanged.removeListener(onSettingsChanged);
        window.navigation?.removeEventListener("currententrychange", onNavigation);
        window.removeEventListener("pageshow", onPageShow);
        chrome.runtime.onMessage.removeListener(onMessage);
        chrome.runtime.onConnect.removeListener(onConnect);
        previewPort?.disconnect();
        document.dispatchEvent(new Event("zitter-night-shift-uninstall-proxy"));
        delete globalThis.zitterNightShiftActive;
    }

    chrome.storage.onChanged.addListener(onSettingsChanged);
    chrome.runtime.onMessage.addListener(onMessage);
    chrome.runtime.onConnect.addListener(onConnect);
    window.navigation?.addEventListener("currententrychange", onNavigation);
    window.addEventListener("pageshow", onPageShow);
    refresh();
})();
