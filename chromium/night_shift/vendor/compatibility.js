/* Derived from Dark Reader 4.9.135. See LICENSE. */

// src/utils/text.ts
function parseArray(text) {
  return text.replace(/\r/g, "").split("\n").map((s) => s.trim()).filter((s) => s);
}

// src/utils/cache.ts
function cachedFactory(factory, size) {
  const cache = /* @__PURE__ */ new Map();
  return (key) => {
    if (cache.has(key)) {
      return cache.get(key);
    }
    const value = factory(key);
    cache.set(key, value);
    if (cache.size > size) {
      const first = cache.keys().next().value;
      cache.delete(first);
    }
    return value;
  };
}

// src/utils/url.ts
function isURLInList(url, list) {
  for (let i = 0; i < list.length; i++) {
    if (isURLMatched(url, list[i])) {
      return true;
    }
  }
  return false;
}
function isURLMatched(url, urlTemplate) {
  if (isRegExp(urlTemplate)) {
    const regexp = createRegExp(urlTemplate);
    return regexp ? regexp.test(url) : false;
  }
  return matchURLPattern(url, urlTemplate);
}
var URL_CACHE_SIZE = 32;
var prepareURL = cachedFactory((url) => {
  let parsed;
  try {
    parsed = new URL(url);
  } catch (err) {
    return null;
  }
  const { hostname, pathname, protocol, port } = parsed;
  const hostParts = hostname.split(".").reverse();
  const pathParts = pathname.split("/").slice(1);
  if (!pathParts[pathParts.length - 1]) {
    pathParts.splice(pathParts.length - 1, 1);
  }
  return {
    hostParts,
    pathParts,
    port,
    protocol
  };
}, URL_CACHE_SIZE);
var URL_MATCH_CACHE_SIZE = 32 * 1024;
var preparePattern = cachedFactory((pattern) => {
  if (!pattern) {
    return null;
  }
  const exactStart = pattern.startsWith("^");
  const exactEnd = pattern.endsWith("$");
  if (exactStart) {
    pattern = pattern.substring(1);
  }
  if (exactEnd) {
    pattern = pattern.substring(0, pattern.length - 1);
  }
  let protocol = "";
  const protocolIndex = pattern.indexOf("://");
  if (protocolIndex > 0) {
    protocol = pattern.substring(0, protocolIndex + 1);
    pattern = pattern.substring(protocolIndex + 3);
  }
  const slashIndex = pattern.indexOf("/");
  const host = slashIndex < 0 ? pattern : pattern.substring(0, slashIndex);
  let hostName = host;
  let isIPv6 = false;
  let ipV6End = -1;
  if (host.startsWith("[")) {
    ipV6End = host.indexOf("]");
    if (ipV6End > 0) {
      isIPv6 = true;
    }
  }
  let port = "*";
  const portIndex = host.lastIndexOf(":");
  if (portIndex >= 0 && (!isIPv6 || ipV6End < portIndex)) {
    hostName = host.substring(0, portIndex);
    port = host.substring(portIndex + 1);
  }
  if (isIPv6) {
    try {
      const ipV6URL = new URL(`http://${hostName}`);
      hostName = ipV6URL.hostname;
    } catch (err) {
    }
  }
  const hostParts = hostName.split(".").reverse();
  const path = slashIndex < 0 ? "" : pattern.substring(slashIndex + 1);
  const pathParts = path.split("/");
  if (!pathParts[pathParts.length - 1]) {
    pathParts.splice(pathParts.length - 1, 1);
  }
  return {
    hostParts,
    pathParts,
    port,
    exactStart,
    exactEnd,
    protocol
  };
}, URL_MATCH_CACHE_SIZE);
function matchURLPattern(url, pattern) {
  const u = prepareURL(url);
  const p = preparePattern(pattern);
  return matchPreparedURLPattern(u, p);
}
function matchPreparedURLPattern(u, p) {
  if (!(u && p) || p.hostParts.length > u.hostParts.length || p.exactStart && p.hostParts.length !== u.hostParts.length || p.exactEnd && p.pathParts.length !== u.pathParts.length || p.port !== "*" && p.port !== u.port || p.protocol && p.protocol !== u.protocol) {
    return false;
  }
  for (let i = 0; i < p.hostParts.length; i++) {
    const pHostPart = p.hostParts[i];
    const uHostPart = u.hostParts[i];
    if (pHostPart !== "*" && pHostPart !== uHostPart) {
      return false;
    }
  }
  if (p.hostParts.length >= 2 && p.hostParts.at(-1) !== "*" && (p.hostParts.length < u.hostParts.length - 1 || p.hostParts.length === u.hostParts.length - 1 && u.hostParts.at(-1) !== "www")) {
    return false;
  }
  if (p.pathParts.length === 0) {
    return true;
  }
  if (p.pathParts.length > u.pathParts.length) {
    return false;
  }
  for (let i = 0; i < p.pathParts.length; i++) {
    const pPathPart = p.pathParts[i];
    const uPathPart = u.pathParts[i];
    if (pPathPart !== "*" && pPathPart !== uPathPart) {
      return false;
    }
  }
  return true;
}
function isRegExp(pattern) {
  return pattern.startsWith("/") && pattern.endsWith("/") && pattern.length > 2;
}
var REGEXP_CACHE_SIZE = 1024;
var createRegExp = cachedFactory((pattern) => {
  if (pattern.startsWith("/")) {
    pattern = pattern.substring(1);
  }
  if (pattern.endsWith("/")) {
    pattern = pattern.substring(0, pattern.length - 1);
  }
  try {
    return new RegExp(pattern);
  } catch (err) {
    return null;
  }
}, REGEXP_CACHE_SIZE);
function indexURLTemplateList(list, assign = () => true) {
  const trie = {
    key: "",
    hostNodes: /* @__PURE__ */ new Map(),
    pathNodes: /* @__PURE__ */ new Map(),
    hardPatterns: [],
    regexps: [],
    data: null
  };
  const templateIndices = /* @__PURE__ */ new Map();
  const patterns = [];
  list.forEach((u, i) => {
    if (isRegExp(u)) {
      const r = createRegExp(u);
      if (r) {
        trie.regexps.push({ regexp: r, data: assign(list[i], i) });
      }
    } else {
      const p = preparePattern(u);
      if (p) {
        if (p.exactStart || p.exactEnd || p.port && p.port !== "*" || p.protocol) {
          trie.hardPatterns.push({ pattern: p, data: assign(list[i], i) });
          return;
        }
        patterns.push(p);
        templateIndices.set(p, i);
      }
    }
  });
  patterns.forEach((pattern) => {
    const listIndex = templateIndices.get(pattern);
    const data = assign(list[listIndex], listIndex);
    let node = trie;
    pattern.hostParts.forEach((p) => {
      const nodes = node.hostNodes;
      if (nodes.has(p)) {
        node = nodes.get(p);
      } else {
        node = {
          key: p,
          hostNodes: /* @__PURE__ */ new Map(),
          pathNodes: /* @__PURE__ */ new Map(),
          data: null
        };
        nodes.set(p, node);
      }
    });
    let lastHostNode = node.hostNodes.get("");
    if (!lastHostNode) {
      lastHostNode = {
        key: "",
        hostNodes: /* @__PURE__ */ new Map(),
        pathNodes: /* @__PURE__ */ new Map(),
        data: null
      };
      node.hostNodes.set("", lastHostNode);
    }
    node = lastHostNode;
    if (pattern.pathParts.length === 0) {
      node.data = data;
      return;
    }
    pattern.pathParts.forEach((p) => {
      const nodes = node.pathNodes;
      if (nodes.has(p)) {
        node = nodes.get(p);
      } else {
        node = {
          key: p,
          hostNodes: /* @__PURE__ */ new Map(),
          pathNodes: /* @__PURE__ */ new Map(),
          data: null
        };
        nodes.set(p, node);
      }
    });
    let lastPathNode = node.pathNodes.get("");
    if (!lastPathNode) {
      lastPathNode = {
        key: "",
        hostNodes: /* @__PURE__ */ new Map(),
        pathNodes: /* @__PURE__ */ new Map(),
        data: null
      };
      node.pathNodes.set("", lastPathNode);
    }
    lastPathNode.data = data;
  });
  return trie;
}
function isURLInIndexedList(url, trie) {
  const matches = getURLMatchesFromIndexedList(url, trie, true);
  return matches.length > 0;
}
function getURLMatchesFromIndexedList(url, trie, breakOnFirstMatch = false) {
  const found = /* @__PURE__ */ new Set();
  const matches = [];
  const push2 = (data) => {
    if (!found.has(data)) {
      found.add(data);
      matches.push(data);
    }
  };
  for (const r of trie.regexps) {
    if (r.regexp.test(url)) {
      push2(r.data);
      if (breakOnFirstMatch) {
        return matches;
      }
    }
  }
  const u = prepareURL(url);
  if (!u) {
    return matches;
  }
  for (const p of trie.hardPatterns) {
    if (matchPreparedURLPattern(u, p.pattern)) {
      push2(p.data);
      if (breakOnFirstMatch) {
        return matches;
      }
    }
  }
  const matchHost = (node, index) => {
    const finalHostNode = node.hostNodes.get("");
    const noMoreHostParts = index === u.hostParts.length;
    const value = noMoreHostParts ? "" : u.hostParts[index];
    if (finalHostNode && (noMoreHostParts || node.key === "*" || index === u.hostParts.length - 1 && value === "www")) {
      if (finalHostNode.data) {
        push2(finalHostNode.data);
        if (breakOnFirstMatch) {
          return;
        }
      }
      matchPath(finalHostNode, 0);
    }
    if (noMoreHostParts) {
      return;
    }
    const nodes = node.hostNodes;
    const wildcardNode = nodes.get("*");
    if (wildcardNode) {
      matchHost(wildcardNode, index + 1);
    }
    if (breakOnFirstMatch && matches.length > 0) {
      return;
    }
    const keyNode = nodes.get(value);
    if (keyNode) {
      matchHost(keyNode, index + 1);
    }
  };
  const matchPath = (node, index) => {
    const finalPathNode = node.pathNodes.get("");
    const noMorePathParts = index === u.pathParts.length;
    const value = noMorePathParts ? "" : u.pathParts[index];
    if (finalPathNode && finalPathNode.data) {
      push2(finalPathNode.data);
    }
    if (noMorePathParts) {
      return;
    }
    const nodes = node.pathNodes;
    const wildcardNode = nodes.get("*");
    if (wildcardNode) {
      matchPath(wildcardNode, index + 1);
    }
    if (breakOnFirstMatch && matches.length > 0) {
      return;
    }
    const keyNode = nodes.get(value);
    if (keyNode) {
      matchPath(keyNode, index + 1);
    }
  };
  matchHost(trie, 0);
  return matches;
}

// src/generators/utils/parse.ts
function parseSitesFixesConfig(text, options) {
  const sites = [];
  const blocks = text.replace(/\r/g, "").split(/^\s*={2,}\s*$/gm);
  blocks.forEach((block) => {
    const lines = block.split("\n");
    const commandIndices = [];
    lines.forEach((ln, i) => {
      if (ln.match(/^[A-Z]+(\s[A-Z]+){0,2}$/)) {
        commandIndices.push(i);
      }
    });
    if (commandIndices.length === 0) {
      return;
    }
    const siteFix = {
      url: parseArray(lines.slice(0, commandIndices[0]).join("\n"))
    };
    commandIndices.forEach((commandIndex, i) => {
      const command = lines[commandIndex].trim();
      const valueText = lines.slice(commandIndex + 1, i === commandIndices.length - 1 ? lines.length : commandIndices[i + 1]).join("\n");
      const prop = options.getCommandPropName(command);
      if (!prop) {
        return;
      }
      const value = options.parseCommandValue(command, valueText);
      siteFix[prop] = value;
    });
    sites.push(siteFix);
  });
  return sites;
}
function processSiteFixesConfigBlock(text, offsets, recordStart, recordEnd, urls) {
  const block = text.substring(recordStart, recordEnd);
  const lines = block.split("\n");
  const commandIndices = [];
  lines.forEach((ln, i) => {
    if (ln.match(/^[A-Z]+(\s[A-Z]+){0,2}$/)) {
      commandIndices.push(i);
    }
  });
  if (commandIndices.length === 0) {
    return;
  }
  offsets.push([recordStart, recordEnd - recordStart]);
  const urls_ = parseArray(lines.slice(0, commandIndices[0]).join("\n"));
  urls.push(urls_);
}
function extractURLsFromSiteFixesConfig(text) {
  const urls = [];
  const offsets = [];
  let recordStart = 0;
  const delimiterRegex = /^\s*={2,}\s*$/gm;
  let delimiter;
  while (delimiter = delimiterRegex.exec(text)) {
    const nextDelimiterStart = delimiter.index;
    const nextDelimiterEnd = delimiter.index + delimiter[0].length;
    processSiteFixesConfigBlock(text, offsets, recordStart, nextDelimiterStart, urls);
    recordStart = nextDelimiterEnd;
  }
  processSiteFixesConfigBlock(text, offsets, recordStart, text.length, urls);
  return { urls, offsets };
}
function indexSitesFixesConfig(text) {
  const { urls, offsets: offsetsGrouped } = extractURLsFromSiteFixesConfig(text);
  const offsetMap = /* @__PURE__ */ new Map();
  const templates = [];
  const offsets = [];
  urls.forEach((block, i) => {
    block.forEach((u) => {
      templates.push(u);
      offsets.push(offsetsGrouped[i]);
      offsetMap.set(u, offsetsGrouped[i]);
    });
  });
  const indexedList = indexURLTemplateList(templates, (_, i) => {
    return offsets[i];
  });
  return indexedList;
}
var siteFixesCache = /* @__PURE__ */ new WeakMap();
function getSitesFixesFor(url, text, index, parse) {
  const matches = getURLMatchesFromIndexedList(url, index);
  const fixes = matches.map((offset) => {
    const cache = siteFixesCache.get(offset);
    if (cache) {
      return cache;
    }
    const [start, length] = offset;
    const block = text.slice(start, start + length);
    const fix = parse(block)[0];
    siteFixesCache.set(offset, fix);
    return fix;
  });
  if (fixes.length > 0) {
    const commonFixIndex = fixes.findIndex((f) => f.url?.[0] === "*");
    if (commonFixIndex > 0) {
      const commonFix = fixes[commonFixIndex];
      fixes.splice(commonFixIndex, 1);
      fixes.unshift(commonFix);
    }
  }
  return fixes;
}

// src/generators/dynamic-theme.ts
var dynamicThemeFixesCommands = {
  "INVERT": "invert",
  "CSS": "css",
  "IGNORE INLINE STYLE": "ignoreInlineStyle",
  "IGNORE IMAGE ANALYSIS": "ignoreImageAnalysis",
  "IGNORE CSS URL": "ignoreCSSUrl"
};
function parseDynamicThemeFixes(text) {
  return parseSitesFixesConfig(text, {
    commands: Object.keys(dynamicThemeFixesCommands),
    getCommandPropName: (command) => dynamicThemeFixesCommands[command],
    parseCommandValue: (command, value) => {
      if (command === "CSS") {
        return value.trim();
      }
      return parseArray(value);
    }
  });
}

// src/inject/utils/log.ts
function logWarn(...args) {
  if (false) {
    console.log("DARK READER", ...args);
    sendLogToBG("warn", ...args);
  }
}

// src/inject/dynamic-theme/fixes.ts
function findRelevantFix(documentURL, fixes) {
  if (!Array.isArray(fixes) || fixes.length === 0 || fixes[0].url[0] !== "*") {
    logWarn("selectRelevantFix() failed to construct a single fix", documentURL, fixes);
    return null;
  }
  let maxSpecificity = 0;
  let maxSpecificityIndex = null;
  for (let i = 1; i < fixes.length; i++) {
    if (isURLInList(documentURL, fixes[i].url)) {
      const specificity = fixes[i].url[0].length;
      if (maxSpecificityIndex === null || maxSpecificity < specificity) {
        maxSpecificity = specificity;
        maxSpecificityIndex = i;
      }
    }
  }
  return maxSpecificityIndex;
}
function combineFixes(fixes) {
  if (fixes.length === 0 || fixes[0].url[0] !== "*") {
    logWarn("combineFixes() failed to construct a single fix", fixes);
    return null;
  }
  function combineArrays(arrays) {
    return arrays.filter(Boolean).flat();
  }
  return {
    url: [],
    invert: combineArrays(fixes.map((fix) => fix.invert)),
    css: fixes.map((fix) => fix.css).filter(Boolean).join("\n"),
    ignoreInlineStyle: combineArrays(fixes.map((fix) => fix.ignoreInlineStyle)),
    ignoreImageAnalysis: combineArrays(fixes.map((fix) => fix.ignoreImageAnalysis)),
    ignoreCSSUrl: combineArrays(fixes.map((fix) => fix.ignoreCSSUrl)),
    disableStyleSheetsProxy: fixes.some((fix) => fix.disableStyleSheetsProxy),
    disableCustomElementRegistryProxy: fixes.some((fix) => fix.disableCustomElementRegistryProxy)
  };
}

// vendor_bridge.ts
function compatibility(fixText, darkSitesText) {
  const fixIndex = indexSitesFixesConfig(fixText);
  const darkSites = indexURLTemplateList(darkSitesText.trim().split(/\s+/));
  return (url) => {
    const fixes = getSitesFixesFor(url, fixText, fixIndex, parseDynamicThemeFixes);
    const match = findRelevantFix(url, fixes);
    return {
      dark: isURLInIndexedList(url, darkSites),
      fixes: combineFixes(match === null ? fixes.slice(0, 1) : [fixes[0], fixes[match]])
    };
  };
}
export {
  compatibility
};
