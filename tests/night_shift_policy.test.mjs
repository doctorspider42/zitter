// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import test from "node:test";
import "../chromium/night_shift/site_policy.js";

const {siteKey, siteSettings, storedSite, defaults, repair, repairCss, active} = globalThis.NightShiftPolicy;
test("site appearance overrides the profile default independently of native dark forcing", () => {
    for (const enabled of [false, true]) {
        assert.equal(active(enabled, siteSettings({mode: "auto"})), enabled);
        assert.equal(active(enabled, siteSettings({mode: "off"})), false);
        assert.equal(active(enabled, siteSettings({mode: "on"})), true);
        assert.equal(active(enabled, siteSettings({mode: "dark"})), true);
    }
    assert.equal(siteSettings({mode: "dark"}, {force: true}).force, false);
    assert.equal(siteSettings({mode: "on"}).force, true);
    assert.equal(storedSite({mode: "dark", brightness: 105}).mode, "dark");
});
const caption = {
    path: [{tag: "article", id: "story"}, {tag: "figcaption", classes: ["muted"]}],
    property: "color",
    color: "#EEDDAA",
};

test("site keys share a hostname across schemes and retain distinct ports", () => {
    assert.equal(siteKey("https://Example.test./story"), "site:example.test");
    assert.equal(siteKey("http://example.test/other"), "site:example.test");
    assert.equal(siteKey("https://example.test:8443/story"), "site:example.test:8443");
    assert.notEqual(siteKey("https://docs.example.test"), siteKey("https://example.test"));
    assert.equal(siteKey("chrome-extension://fake/options.html"), null);
    assert.equal(siteKey(undefined), null);
});

test("invalid or older site data falls back to automatic mode and default tuning", () => {
    assert.deepEqual(siteSettings({mode: "filter", brightness: 900, contrast: "115"}), {
        mode: "auto", force: false, brightness: 100, contrast: 100, sepia: 0, grayscale: 0,
        repair: null, previousRepair: null,
    });
    assert.equal(siteSettings({mode: "off", brightness: 75, contrast: 125}).brightness, 75);
    assert.equal(siteSettings({mode: "on", brightness: 75, contrast: 125}).contrast, 125);
    assert.equal(siteSettings({mode: "off", force: true}).force, true);
});

test("app-wide defaults fill only the values a site did not tune", () => {
    const look = {brightness: 90, contrast: 110, sepia: 20, grayscale: 300, force: true};
    assert.deepEqual(defaults(look), {brightness: 90, contrast: 110, sepia: 20, grayscale: 0, force: true});
    const untuned = siteSettings({mode: "auto"}, look);
    assert.equal(untuned.brightness, 90);
    assert.equal(untuned.contrast, 110);
    assert.equal(untuned.sepia, 20);
    assert.equal(untuned.force, true);
    const tuned = siteSettings({mode: "auto", brightness: 120}, look);
    assert.equal(tuned.brightness, 120);
    assert.equal(tuned.contrast, 110);
    assert.equal(siteSettings({mode: "off"}, look).force, false);
    assert.deepEqual(storedSite({mode: "auto", contrast: 80, brightness: 1}), {
        mode: "auto", force: false, repair: null, previousRepair: null, contrast: 80,
    });
});

test("typed repairs compile to a scoped color override", () => {
    assert.equal(
        repairCss({label: "Caption contrast", rules: [caption]}),
        "article#story > figcaption.muted { color: #eeddaa !important; }",
    );
});

test("repair validation rejects selectors, values and properties that can change page behavior", () => {
    for (const rule of [
        {...caption, path: [{tag: "figcaption", id: "caption, body"}]},
        {...caption, path: [{tag: "figcaption", classes: ["x}body{"]}]},
        {...caption, property: "background-image", color: "url(https://example.test)"},
        {...caption, property: "display"},
        {...caption, color: "#ffffff; opacity: 0"},
    ]) {
        assert.equal(repair({label: "Unsafe rule", rules: [rule]}), null);
    }
    assert.equal(repair({label: "Duplicate", rules: [caption, caption]}), null);
    assert.equal(repairCss({label: "Control\u202e", rules: [caption]}), "");
});
