// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
import {injectProxy} from './src/inject/dynamic-theme/stylesheet-proxy';

// Runs in the page's world so CSSStyleSheet calls made by the page are observed.
// The event carries only two feature switches. No extension APIs are exposed.
let installed = false;
const listeners = new AbortController();
const options = {signal: listeners.signal};
document.addEventListener('__darkreader__cleanUp', () => {
    installed = false;
}, options);
document.addEventListener('zitter-night-shift-uninstall-proxy', () => {
    listeners.abort();
}, options);
document.addEventListener('zitter-night-shift-install-proxy', (event: Event) => {
    if (installed || !(event instanceof CustomEvent)) {
        return;
    }
    const flags = event.detail;
    if (!Number.isInteger(flags) || flags < 0 || flags > 3) {
        return;
    }
    installed = true;
    injectProxy(Boolean(flags & 1), Boolean(flags & 2));
}, options);
