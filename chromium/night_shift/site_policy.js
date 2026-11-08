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

    function percentage(value) {
        return Number.isInteger(value) && value >= 75 && value <= 125 ? value : 100;
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

    function siteSettings(value) {
        const data = value ?? {};
        const mode = data.mode === "off" || data.mode === "on" ? data.mode : "auto";
        return {
            mode,
            force: mode === "on" || (mode === "off" && data.force === true),
            brightness: percentage(data.brightness),
            contrast: percentage(data.contrast),
            repair: repair(data.repair),
            previousRepair: repair(data.previousRepair),
        };
    }

    function repairCss(value) {
        return repair(value)?.rules.map(rule =>
            selector(rule.path) + " { " + rule.property + ": " + rule.color + " !important; }"
        ).join("\n") ?? "";
    }

    globalThis.NightShiftPolicy = Object.freeze({siteKey, siteSettings, selector, repair, repairCss});
})();
