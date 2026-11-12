// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// This entry point is built against the pinned upstream source by refresh_darkreader.py.
import {parseDynamicThemeFixes} from './src/generators/dynamic-theme';
import {getSitesFixesFor, indexSitesFixesConfig} from './src/generators/utils/parse';
import {combineFixes, findRelevantFix} from './src/inject/dynamic-theme/fixes';
import {indexURLTemplateList, isURLInIndexedList} from './src/utils/url';

export function compatibility(fixText: string, darkSitesText: string) {
    const fixIndex = indexSitesFixesConfig(fixText);
    const darkSites = indexURLTemplateList(darkSitesText.trim().split(/\s+/));
    return (url: string) => {
        const fixes = getSitesFixesFor(url, fixText, fixIndex, parseDynamicThemeFixes);
        const match = findRelevantFix(url, fixes);
        return {
            dark: isURLInIndexedList(url, darkSites),
            fixes: combineFixes(match === null ? fixes.slice(0, 1) : [fixes[0], fixes[match]]),
        };
    };
}
