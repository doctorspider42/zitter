// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0

(() => {
    "use strict";

    const COLOR_PROPERTIES = new Set([
        "background-color", "border-color", "color", "fill", "outline-color",
        "stroke", "text-decoration-color",
    ]);
    const IDENTIFIER = /^[a-z_][\w-]{0,63}$/i;
    const ELEMENT = /^[a-z][a-z0-9-]{0,31}$/;
    const COLOR = /^(?:#[0-9a-f]{6}|#[0-9a-f]{8}|transparent)$/i;
    const MAX_RULES = 12;

    function siteKey(url) {
        try {
            const parsed = new URL(url);
            if (parsed.protocol === "http:" || parsed.protocol === "https:") {
                return "site:" + parsed.host.replace(/\.(?=:|$)/, "");
            }
        } catch {
            // An internal page or provisional navigation has no site settings.
        }
        return null;
    }

    function bounded(value, low, high, fallback) {
        return Number.isInteger(value) && value >= low && value <= high ? value : fallback;
    }

    function percentage(value, fallback = 100) {
        return bounded(value, 75, 125, fallback);
    }

    // The look every site without its own tuning gets. Zitter writes it for
    // the whole app from its Night Shift settings.
    function defaults(value) {
        const data = value ?? {};
        return {
            brightness: percentage(data.brightness),
            contrast: percentage(data.contrast),
            sepia: bounded(data.sepia, 0, 50, 0),
            grayscale: bounded(data.grayscale, 0, 100, 0),
            force: data.force === true,
        };
    }

    function selectorPart(value) {
        if (!value || typeof value.tag !== "string" || !ELEMENT.test(value.tag)) {
            return null;
        }
        if (value.id != null && (typeof value.id !== "string" || !IDENTIFIER.test(value.id))) {
            return null;
        }
        const classes = value.classes ?? [];
        if (!Array.isArray(classes) || classes.length > 3 ||
            classes.some(name => typeof name !== "string" || !IDENTIFIER.test(name))) {
            return null;
        }
        const index = value.index;
        if (index != null && (!Number.isInteger(index) || index < 1 || index > 500)) {
            return null;
        }
        return value.tag + (value.id ? "#" + value.id : "") +
            classes.map(name => "." + name).join("") + (index ? ":nth-of-type(" + index + ")" : "");
    }

    function selector(path) {
        if (!Array.isArray(path) || path.length < 1 || path.length > 5) {
            return null;
        }
        const parts = path.map(selectorPart);
        return parts.every(Boolean) ? parts.join(" > ") : null;
    }

    function repair(value) {
        if (!value || typeof value.label !== "string" || !Array.isArray(value.rules) ||
            value.rules.length < 1 || value.rules.length > MAX_RULES) {
            return null;
        }
        const label = value.label.trim();
        if (!label || label.length > 100 || /[\u0000-\u001f\u007f\u202a-\u202e\u2066-\u2069]/.test(label)) {
            return null;
        }
        const rules = [];
        const destinations = new Set();
        for (const rule of value.rules) {
            const target = selector(rule?.path);
            if (!target || !COLOR_PROPERTIES.has(rule.property) ||
                typeof rule.color !== "string" || !COLOR.test(rule.color)) {
                return null;
            }
            const destination = target + " " + rule.property;
            if (destinations.has(destination)) {
                return null;
            }
            destinations.add(destination);
            rules.push({
                path: rule.path.map(part => ({
                    tag: part.tag,
                    id: part.id ?? null,
                    classes: part.classes?.slice() ?? [],
                    index: part.index ?? null,
                })),
                property: rule.property,
                color: rule.color.toLowerCase(),
            });
        }
        return {label, rules};
    }

    function siteSettings(value, base) {
        const data = value ?? {};
        const look = defaults(base);
        const mode = ["off", "on", "dark"].includes(data.mode) ? data.mode : "auto";
        return {
            mode,
            force: mode === "on" || (mode === "off" && data.force === true) || (mode === "auto" && look.force),
            brightness: percentage(data.brightness, look.brightness),
            contrast: percentage(data.contrast, look.contrast),
            sepia: look.sepia,
            grayscale: look.grayscale,
            repair: repair(data.repair),
            previousRepair: repair(data.previousRepair),
        };
    }

    // What a site record keeps: tuning only where the person set it, so the
    // app-wide defaults still reach every other value.
    function storedSite(value) {
        const data = value ?? {};
        const settings = siteSettings(data);
        const stored = {
            mode: settings.mode, force: settings.force,
            repair: settings.repair, previousRepair: settings.previousRepair,
        };
        if (percentage(data.brightness, null) !== null) stored.brightness = data.brightness;
        if (percentage(data.contrast, null) !== null) stored.contrast = data.contrast;
        return stored;
    }

    function repairCss(value) {
        return repair(value)?.rules.map(rule =>
            selector(rule.path) + " { " + rule.property + ": " + rule.color + " !important; }"
        ).join("\n") ?? "";
    }

    globalThis.NightShiftPolicy = Object.freeze({
        siteKey, siteSettings, storedSite, defaults, selector, repair, repairCss,
        active: (enabled, settings) => settings.mode === "on" || settings.mode === "dark" ||
            (enabled && settings.mode !== "off"),
    });
})();
