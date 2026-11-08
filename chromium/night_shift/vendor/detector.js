/* Derived from Dark Reader 4.9.135. See LICENSE. */
var NightShiftDetector = (() => {
  var __defProp = Object.defineProperty;
  var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __hasOwnProp = Object.prototype.hasOwnProperty;
  var __export = (target, all) => {
    for (var name in all)
      __defProp(target, name, { get: all[name], enumerable: true });
  };
  var __copyProps = (to, from, except, desc) => {
    if (from && typeof from === "object" || typeof from === "function") {
      for (let key of __getOwnPropNames(from))
        if (!__hasOwnProp.call(to, key) && key !== except)
          __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
    }
    return to;
  };
  var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);

  // src/inject/detector.ts
  var detector_exports = {};
  __export(detector_exports, {
    runDarkThemeDetector: () => runDarkThemeDetector,
    stopDarkThemeDetector: () => stopDarkThemeDetector
  });

  // src/utils/math-eval.ts
  function evalMath(expression) {
    const rpnStack = [];
    const workingStack = [];
    let lastToken;
    for (let i = 0, len = expression.length; i < len; i++) {
      const token = expression[i];
      if (!token || token === " ") {
        continue;
      }
      if (operators.has(token)) {
        const op = operators.get(token);
        while (workingStack.length) {
          const currentOp = operators.get(workingStack[0]);
          if (!currentOp) {
            break;
          }
          if (op.lessOrEqualThan(currentOp)) {
            rpnStack.push(workingStack.shift());
          } else {
            break;
          }
        }
        workingStack.unshift(token);
      } else if (!lastToken || operators.has(lastToken)) {
        rpnStack.push(token);
      } else {
        rpnStack[rpnStack.length - 1] += token;
      }
      lastToken = token;
    }
    rpnStack.push(...workingStack);
    const stack = [];
    for (let i = 0, len = rpnStack.length; i < len; i++) {
      const op = operators.get(rpnStack[i]);
      if (op) {
        const args = stack.splice(0, 2);
        stack.push(op.exec(args[1], args[0]));
      } else {
        stack.unshift(parseFloat(rpnStack[i]));
      }
    }
    return stack[0];
  }
  var Operator = class {
    constructor(precedence, method) {
      this.precendce = precedence;
      this.execMethod = method;
    }
    exec(left, right) {
      return this.execMethod(left, right);
    }
    lessOrEqualThan(op) {
      return this.precendce <= op.precendce;
    }
  };
  var operators = /* @__PURE__ */ new Map([
    ["+", new Operator(1, (left, right) => left + right)],
    ["-", new Operator(1, (left, right) => left - right)],
    ["*", new Operator(2, (left, right) => left * right)],
    ["/", new Operator(2, (left, right) => left / right)]
  ]);

  // src/utils/platform.ts
  var isNavigatorDefined = typeof navigator !== "undefined";
  var userAgent = isNavigatorDefined ? navigator.userAgentData && Array.isArray(navigator.userAgentData.brands) ? navigator.userAgentData.brands.map((brand) => `${brand.brand.toLowerCase()} ${brand.version}`).join(" ") : navigator.userAgent.toLowerCase() : "some useragent";
  var platform = isNavigatorDefined ? navigator.userAgentData && typeof navigator.userAgentData.platform === "string" ? navigator.userAgentData.platform.toLowerCase() : navigator.platform.toLowerCase() : "some platform";
  var isVivaldi = userAgent.includes("vivaldi");
  var isYaBrowser = userAgent.includes("yabrowser");
  var isOpera = userAgent.includes("opr") || userAgent.includes("opera");
  var isEdge = userAgent.includes("edg");
  var isWindows = platform.startsWith("win");
  var isMacOS = platform.startsWith("mac");
  var isMobile = isNavigatorDefined && navigator.userAgentData ? navigator.userAgentData.mobile : userAgent.includes("mobile") || false;
  var isMatchMediaChangeEventListenerBuggy = isNavigatorDefined && navigator.userAgentData && ["Linux", "Android"].includes(navigator.userAgentData.platform) || platform.startsWith("linux");
  var chromiumVersion = (() => {
    const m = userAgent.match(/chrom(?:e|ium)(?:\/| )([^ ]+)/);
    if (m && m[1]) {
      return m[1];
    }
    return "";
  })();
  var firefoxVersion = (() => {
    const m = userAgent.match(/(?:firefox|librewolf)(?:\/| )([^ ]+)/);
    if (m && m[1]) {
      return m[1];
    }
    return "";
  })();
  var isDefinedSelectorSupported = (() => {
    try {
      document.querySelector(":defined");
      return true;
    } catch (err) {
      return false;
    }
  })();

  // src/utils/media-query.ts
  var query = null;
  var isSystemDarkModeEnabled = () => false ? override : (query || matchMedia("(prefers-color-scheme: dark)")).matches;

  // src/utils/text.ts
  function getParenthesesRange(input, searchStartIndex = 0) {
    return getOpenCloseRange(input, searchStartIndex, "(", ")", []);
  }
  function getOpenCloseRange(input, searchStartIndex, openToken, closeToken, excludeRanges) {
    let indexOf;
    if (excludeRanges.length === 0) {
      indexOf = (token, pos) => input.indexOf(token, pos);
    } else {
      indexOf = (token, pos) => indexOfExcluding(input, token, pos, excludeRanges);
    }
    const { length } = input;
    let depth = 0;
    let firstOpenIndex = -1;
    for (let i = searchStartIndex; i < length; i++) {
      if (depth === 0) {
        const openIndex = indexOf(openToken, i);
        if (openIndex < 0) {
          break;
        }
        firstOpenIndex = openIndex;
        depth++;
        i = openIndex;
      } else {
        const closeIndex = indexOf(closeToken, i);
        if (closeIndex < 0) {
          break;
        }
        const openIndex = indexOf(openToken, i);
        if (openIndex < 0 || closeIndex <= openIndex) {
          depth--;
          if (depth === 0) {
            return { start: firstOpenIndex, end: closeIndex + 1 };
          }
          i = closeIndex;
        } else {
          depth++;
          i = openIndex;
        }
      }
    }
    return null;
  }
  function indexOfExcluding(input, search, position, excludeRanges) {
    const i = input.indexOf(search, position);
    const exclusion = excludeRanges.find((r) => i >= r.start && i < r.end);
    if (exclusion) {
      return indexOfExcluding(input, search, exclusion.end, excludeRanges);
    }
    return i;
  }

  // src/utils/color.ts
  var rgbaParseCache = /* @__PURE__ */ new Map();
  function parseColorWithCache($color) {
    $color = $color.trim();
    const key = $color;
    if (rgbaParseCache.has(key)) {
      return rgbaParseCache.get(key);
    }
    if ($color.includes("calc(")) {
      $color = lowerCalcExpression($color);
    }
    const color = parse($color);
    rgbaParseCache.set(key, color);
    return color;
  }
  function hslToRGB({ h, s, l, a = 1 }) {
    if (s === 0) {
      const [r2, b2, g2] = [l, l, l].map((x2) => Math.round(x2 * 255));
      return { r: r2, g: g2, b: b2, a };
    }
    const c = (1 - Math.abs(2 * l - 1)) * s;
    const x = c * (1 - Math.abs(h / 60 % 2 - 1));
    const m = l - c / 2;
    const [r, g, b] = (h < 60 ? [c, x, 0] : h < 120 ? [x, c, 0] : h < 180 ? [0, c, x] : h < 240 ? [0, x, c] : h < 300 ? [x, 0, c] : [c, 0, x]).map((n) => Math.round((n + m) * 255));
    return { r, g, b, a };
  }
  var rgbMatch = /^rgba?\([^\(\)]+\)$/;
  var hslMatch = /^hsla?\([^\(\)]+\)$/;
  var hexMatch = /^#[0-9a-f]+$/i;
  var supportedColorFuncs = [
    "color",
    "color-mix",
    "hwb",
    "lab",
    "lch",
    "oklab",
    "oklch"
  ];
  function parse($color) {
    const c = $color.trim().toLowerCase();
    if (c.includes("(from ")) {
      if (c.indexOf("(from") !== c.lastIndexOf("(from")) {
        return null;
      }
      return domParseColor(c);
    }
    if (c.match(rgbMatch)) {
      if (c.startsWith("rgb(#") || c.startsWith("rgba(#")) {
        if (c.lastIndexOf("rgb") > 0) {
          return null;
        }
        return domParseColor(c);
      }
      return parseRGB(c);
    }
    if (c.match(hslMatch)) {
      return parseHSL(c);
    }
    if (c.match(hexMatch)) {
      return parseHex(c);
    }
    if (knownColors.has(c)) {
      return getColorByName(c);
    }
    if (systemColors.has(c)) {
      return getSystemColor(c);
    }
    if (c === "transparent") {
      return { r: 0, g: 0, b: 0, a: 0 };
    }
    if (c.endsWith(")") && supportedColorFuncs.some(
      (fn) => c.startsWith(fn) && c[fn.length] === "(" && c.lastIndexOf(fn) === 0
    )) {
      return domParseColor(c);
    }
    if (c.startsWith("light-dark(") && c.endsWith(")")) {
      const match = c.match(/^light-dark\(\s*([a-z]+(\(.*\))?),\s*([a-z]+(\(.*\))?)\s*\)$/);
      if (match) {
        const schemeColor = isSystemDarkModeEnabled() ? match[3] : match[1];
        return parse(schemeColor);
      }
    }
    return null;
  }
  var C_0 = "0".charCodeAt(0);
  var C_9 = "9".charCodeAt(0);
  var C_e = "e".charCodeAt(0);
  var C_DOT = ".".charCodeAt(0);
  var C_PLUS = "+".charCodeAt(0);
  var C_MINUS = "-".charCodeAt(0);
  var C_SPACE = " ".charCodeAt(0);
  var C_COMMA = ",".charCodeAt(0);
  var C_SLASH = "/".charCodeAt(0);
  var C_PERCENT = "%".charCodeAt(0);
  function getNumbersFromString(input, range, units) {
    const numbers = [];
    const searchStart = input.indexOf("(") + 1;
    const searchEnd = input.length - 1;
    let numStart = -1;
    let unitStart = -1;
    const push = (matchEnd) => {
      const numEnd = unitStart > -1 ? unitStart : matchEnd;
      const $num = input.slice(numStart, numEnd);
      let n = parseFloat($num);
      const r = range[numbers.length];
      if (unitStart > -1) {
        const unit = input.slice(unitStart, matchEnd);
        const u = units[unit];
        if (u != null) {
          n *= r / u;
        }
      }
      if (r > 1) {
        n = Math.round(n);
      }
      numbers.push(n);
      numStart = -1;
      unitStart = -1;
    };
    for (let i = searchStart; i < searchEnd; i++) {
      const c = input.charCodeAt(i);
      const isNumChar = c >= C_0 && c <= C_9 || c === C_DOT || c === C_PLUS || c === C_MINUS || c === C_e;
      const isDelimiter = c === C_SPACE || c === C_COMMA || c === C_SLASH;
      if (isNumChar) {
        if (numStart === -1) {
          numStart = i;
        }
      } else if (numStart > -1) {
        if (isDelimiter) {
          push(i);
        } else if (unitStart === -1) {
          unitStart = i;
        }
      }
    }
    if (numStart > -1) {
      push(searchEnd);
    }
    return numbers;
  }
  var rgbRange = [255, 255, 255, 1];
  var rgbUnits = { "%": 100 };
  function parseRGB($rgb) {
    const [r, g, b, a = 1] = getNumbersFromString($rgb, rgbRange, rgbUnits);
    if (r == null || g == null || b == null || a == null) {
      return null;
    }
    return { r, g, b, a };
  }
  var hslRange = [360, 1, 1, 1];
  var hslUnits = { "%": 100, "deg": 360, "rad": 2 * Math.PI, "turn": 1 };
  function parseHSL($hsl) {
    const [h, s, l, a = 1] = getNumbersFromString($hsl, hslRange, hslUnits);
    if (h == null || s == null || l == null || a == null) {
      return null;
    }
    return hslToRGB({ h, s, l, a });
  }
  var C_A = "A".charCodeAt(0);
  var C_F = "F".charCodeAt(0);
  var C_a = "a".charCodeAt(0);
  var C_f = "f".charCodeAt(0);
  function parseHex($hex) {
    const length = $hex.length;
    const digitCount = length - 1;
    const isShort = digitCount === 3 || digitCount === 4;
    const isLong = digitCount === 6 || digitCount === 8;
    if (!isShort && !isLong) {
      return null;
    }
    const hex = (i) => {
      const c = $hex.charCodeAt(i);
      if (c >= C_A && c <= C_F) {
        return c + 10 - C_A;
      }
      if (c >= C_a && c <= C_f) {
        return c + 10 - C_a;
      }
      return c - C_0;
    };
    let r;
    let g;
    let b;
    let a = 1;
    if (isShort) {
      r = hex(1) * 17;
      g = hex(2) * 17;
      b = hex(3) * 17;
      if (digitCount === 4) {
        a = hex(4) * 17 / 255;
      }
    } else {
      r = hex(1) * 16 + hex(2);
      g = hex(3) * 16 + hex(4);
      b = hex(5) * 16 + hex(6);
      if (digitCount === 8) {
        a = (hex(7) * 16 + hex(8)) / 255;
      }
    }
    return { r, g, b, a };
  }
  function getColorByName($color) {
    const n = knownColors.get($color);
    return {
      r: n >> 16 & 255,
      g: n >> 8 & 255,
      b: n >> 0 & 255,
      a: 1
    };
  }
  function getSystemColor($color) {
    const n = systemColors.get($color);
    return {
      r: n >> 16 & 255,
      g: n >> 8 & 255,
      b: n >> 0 & 255,
      a: 1
    };
  }
  function lowerCalcExpression(color) {
    let searchIndex = 0;
    const replaceBetweenIndices = (start, end, replacement) => {
      color = color.substring(0, start) + replacement + color.substring(end);
    };
    while ((searchIndex = color.indexOf("calc(")) !== -1) {
      const range = getParenthesesRange(color, searchIndex);
      if (!range) {
        break;
      }
      let slice = color.slice(range.start + 1, range.end - 1);
      const includesPercentage = slice.includes("%");
      slice = slice.split("%").join("");
      const output = Math.round(evalMath(slice));
      replaceBetweenIndices(range.start - 4, range.end, output + (includesPercentage ? "%" : ""));
    }
    return color;
  }
  var knownColors = new Map(Object.entries({
    aliceblue: 15792383,
    antiquewhite: 16444375,
    aqua: 65535,
    aquamarine: 8388564,
    azure: 15794175,
    beige: 16119260,
    bisque: 16770244,
    black: 0,
    blanchedalmond: 16772045,
    blue: 255,
    blueviolet: 9055202,
    brown: 10824234,
    burlywood: 14596231,
    cadetblue: 6266528,
    chartreuse: 8388352,
    chocolate: 13789470,
    coral: 16744272,
    cornflowerblue: 6591981,
    cornsilk: 16775388,
    crimson: 14423100,
    cyan: 65535,
    darkblue: 139,
    darkcyan: 35723,
    darkgoldenrod: 12092939,
    darkgray: 11119017,
    darkgrey: 11119017,
    darkgreen: 25600,
    darkkhaki: 12433259,
    darkmagenta: 9109643,
    darkolivegreen: 5597999,
    darkorange: 16747520,
    darkorchid: 10040012,
    darkred: 9109504,
    darksalmon: 15308410,
    darkseagreen: 9419919,
    darkslateblue: 4734347,
    darkslategray: 3100495,
    darkslategrey: 3100495,
    darkturquoise: 52945,
    darkviolet: 9699539,
    deeppink: 16716947,
    deepskyblue: 49151,
    dimgray: 6908265,
    dimgrey: 6908265,
    dodgerblue: 2003199,
    firebrick: 11674146,
    floralwhite: 16775920,
    forestgreen: 2263842,
    fuchsia: 16711935,
    gainsboro: 14474460,
    ghostwhite: 16316671,
    gold: 16766720,
    goldenrod: 14329120,
    gray: 8421504,
    grey: 8421504,
    green: 32768,
    greenyellow: 11403055,
    honeydew: 15794160,
    hotpink: 16738740,
    indianred: 13458524,
    indigo: 4915330,
    ivory: 16777200,
    khaki: 15787660,
    lavender: 15132410,
    lavenderblush: 16773365,
    lawngreen: 8190976,
    lemonchiffon: 16775885,
    lightblue: 11393254,
    lightcoral: 15761536,
    lightcyan: 14745599,
    lightgoldenrodyellow: 16448210,
    lightgray: 13882323,
    lightgrey: 13882323,
    lightgreen: 9498256,
    lightpink: 16758465,
    lightsalmon: 16752762,
    lightseagreen: 2142890,
    lightskyblue: 8900346,
    lightslategray: 7833753,
    lightslategrey: 7833753,
    lightsteelblue: 11584734,
    lightyellow: 16777184,
    lime: 65280,
    limegreen: 3329330,
    linen: 16445670,
    magenta: 16711935,
    maroon: 8388608,
    mediumaquamarine: 6737322,
    mediumblue: 205,
    mediumorchid: 12211667,
    mediumpurple: 9662683,
    mediumseagreen: 3978097,
    mediumslateblue: 8087790,
    mediumspringgreen: 64154,
    mediumturquoise: 4772300,
    mediumvioletred: 13047173,
    midnightblue: 1644912,
    mintcream: 16121850,
    mistyrose: 16770273,
    moccasin: 16770229,
    navajowhite: 16768685,
    navy: 128,
    oldlace: 16643558,
    olive: 8421376,
    olivedrab: 7048739,
    orange: 16753920,
    orangered: 16729344,
    orchid: 14315734,
    palegoldenrod: 15657130,
    palegreen: 10025880,
    paleturquoise: 11529966,
    palevioletred: 14381203,
    papayawhip: 16773077,
    peachpuff: 16767673,
    peru: 13468991,
    pink: 16761035,
    plum: 14524637,
    powderblue: 11591910,
    purple: 8388736,
    rebeccapurple: 6697881,
    red: 16711680,
    rosybrown: 12357519,
    royalblue: 4286945,
    saddlebrown: 9127187,
    salmon: 16416882,
    sandybrown: 16032864,
    seagreen: 3050327,
    seashell: 16774638,
    sienna: 10506797,
    silver: 12632256,
    skyblue: 8900331,
    slateblue: 6970061,
    slategray: 7372944,
    slategrey: 7372944,
    snow: 16775930,
    springgreen: 65407,
    steelblue: 4620980,
    tan: 13808780,
    teal: 32896,
    thistle: 14204888,
    tomato: 16737095,
    turquoise: 4251856,
    violet: 15631086,
    wheat: 16113331,
    white: 16777215,
    whitesmoke: 16119285,
    yellow: 16776960,
    yellowgreen: 10145074
  }));
  var systemColors = new Map(Object.entries({
    ActiveBorder: 3906044,
    ActiveCaption: 0,
    AppWorkspace: 11184810,
    Background: 6513614,
    ButtonFace: 16777215,
    ButtonHighlight: 15329769,
    ButtonShadow: 10461343,
    ButtonText: 0,
    CaptionText: 0,
    GrayText: 8355711,
    Highlight: 11720703,
    HighlightText: 0,
    InactiveBorder: 16777215,
    InactiveCaption: 16777215,
    InactiveCaptionText: 0,
    InfoBackground: 16514245,
    InfoText: 0,
    Menu: 16185078,
    MenuText: 16777215,
    Scrollbar: 11184810,
    ThreeDDarkShadow: 0,
    ThreeDFace: 12632256,
    ThreeDHighlight: 16777215,
    ThreeDLightShadow: 16777215,
    ThreeDShadow: 0,
    Window: 15527148,
    WindowFrame: 11184810,
    WindowText: 0,
    "-webkit-focus-ring-color": 15046400
  }).map(([key, value]) => [key.toLowerCase(), value]));
  function getSRGBLightness(r, g, b) {
    return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
  }
  var canvas;
  var context;
  function domParseColor($color) {
    if (!context) {
      canvas = document.createElement("canvas");
      canvas.width = 1;
      canvas.height = 1;
      context = canvas.getContext("2d", { willReadFrequently: true });
    }
    context.fillStyle = $color;
    context.fillRect(0, 0, 1, 1);
    const d = context.getImageData(0, 0, 1, 1).data;
    const color = `rgba(${d[0]}, ${d[1]}, ${d[2]}, ${(d[3] / 255).toFixed(2)})`;
    return parseRGB(color);
  }

  // src/inject/detector.ts
  var COLOR_SCHEME_META_SELECTOR = 'meta[name="color-scheme"]';
  function hasBuiltInDarkTheme() {
    const rootStyle = getComputedStyle(document.documentElement);
    if (rootStyle.filter.includes("invert(1)") || rootStyle.colorScheme === "dark") {
      return true;
    }
    const CELL_SIZE = 256;
    const MAX_ROW_COUNT = 4;
    const winWidth = innerWidth;
    const winHeight = innerHeight;
    const stepX = Math.floor(winWidth / Math.min(MAX_ROW_COUNT, Math.ceil(winWidth / CELL_SIZE)));
    const stepY = Math.floor(winHeight / Math.min(MAX_ROW_COUNT, Math.ceil(winHeight / CELL_SIZE)));
    const processedElements = /* @__PURE__ */ new Set();
    for (let y = Math.floor(stepY / 2); y < winHeight; y += stepY) {
      for (let x = Math.floor(stepX / 2); x < winWidth; x += stepX) {
        const element = document.elementFromPoint(x, y);
        if (!element || processedElements.has(element) || element.tagName.toLocaleLowerCase() === "img") {
          continue;
        }
        processedElements.add(element);
        const style = element === document.documentElement ? rootStyle : getComputedStyle(element);
        const bgColor = parseColorWithCache(style.backgroundColor);
        if (!bgColor) {
          return false;
        }
        if (bgColor.r === 24 && bgColor.g === 26 && bgColor.b === 27) {
          return false;
        }
        if (bgColor.a === 1) {
          const bgLightness = getSRGBLightness(bgColor.r, bgColor.g, bgColor.b);
          if (bgLightness > 0.6) {
            return false;
          }
        } else {
          const textColor = parseColorWithCache(style.color);
          if (!textColor) {
            return false;
          }
          const textLightness = getSRGBLightness(textColor.r, textColor.g, textColor.b);
          if (textLightness < 0.4) {
            return false;
          }
        }
      }
    }
    const rootColor = parseColorWithCache(rootStyle.backgroundColor);
    if (!rootColor) {
      return false;
    }
    const bodyColor = document.body ? parseColorWithCache(getComputedStyle(document.body).backgroundColor) : { r: 0, g: 0, b: 0, a: 0 };
    if (!bodyColor) {
      return false;
    }
    if (rootColor.a === 0 && bodyColor.a === 0) {
      const rootTextColor = parseColorWithCache(rootStyle.color);
      if (rootTextColor) {
        const textLightness = getSRGBLightness(rootTextColor.r, rootTextColor.g, rootTextColor.b);
        return textLightness > 0.5;
      }
    }
    const rootLightness = 1 - rootColor.a + rootColor.a * getSRGBLightness(rootColor.r, rootColor.g, rootColor.b);
    const finalLightness = (1 - bodyColor.a) * rootLightness + bodyColor.a * getSRGBLightness(bodyColor.r, bodyColor.g, bodyColor.b);
    return finalLightness < 0.5;
  }
  function runCheck(callback) {
    const colorSchemeMeta = document.querySelector(COLOR_SCHEME_META_SELECTOR);
    if (colorSchemeMeta) {
      const content = colorSchemeMeta.content.toLocaleLowerCase();
      const isStrictlyDark = content === "dark" || content === "only dark";
      const isStrictlyLight = content === "light" || content === "only light";
      if (isStrictlyDark || isStrictlyLight) {
        callback(isStrictlyDark);
        return;
      }
    }
    if (document.documentElement.classList.contains("dark") || document.body?.classList.contains("dark") || document.documentElement.dataset.theme?.toLocaleLowerCase() === "dark") {
      callback(true);
      return;
    }
    const drSheets = Array.from(document.styleSheets).filter((s) => s.ownerNode?.classList.contains("darkreader")).concat(
      Array.isArray(document.adoptedStyleSheets) ? Array.from(document.adoptedStyleSheets).filter(
        (s) => s.cssRules?.[0]?.selectorText?.startsWith("#__darkreader")
      ) : []
    );
    drSheets.forEach((sheet) => sheet.disabled = true);
    const darkThemeDetected = hasBuiltInDarkTheme();
    drSheets.forEach((sheet) => sheet.disabled = false);
    callback(darkThemeDetected);
  }
  function hasSomeStyle() {
    if (document.querySelector(COLOR_SCHEME_META_SELECTOR) != null) {
      return true;
    }
    if (document.documentElement.style.backgroundColor || document.body && document.body.style.backgroundColor) {
      return true;
    }
    for (const style of document.styleSheets) {
      if (style && style.ownerNode && !(style.ownerNode.classList && style.ownerNode.classList.contains("darkreader"))) {
        return true;
      }
    }
    return false;
  }
  var observer;
  var readyStateListener;
  function canCheckForStyle() {
    if (!(document.body && document.body.scrollHeight >= 32 && document.body.clientHeight >= 32 && document.body.childElementCount > 0 && hasSomeStyle())) {
      return false;
    }
    for (const child of document.body.children) {
      if (child.tagName !== "SCRIPT" && child.tagName !== "STYLE" && child.tagName !== "LINK") {
        return true;
      }
    }
    return false;
  }
  function runDarkThemeDetector(callback, hints) {
    stopDarkThemeDetector();
    if (hints && hints.length > 0) {
      const hint = hints[0];
      if (hint.noDarkTheme) {
        callback(false);
        return;
      }
      if (hint.systemTheme && isSystemDarkModeEnabled()) {
        callback(true);
        return;
      }
      detectUsingHint(hint, () => callback(true));
      return;
    }
    if (canCheckForStyle()) {
      runCheck(callback);
      return;
    }
    observer = new MutationObserver(() => {
      if (canCheckForStyle()) {
        stopDarkThemeDetector();
        runCheck(callback);
      }
    });
    observer.observe(document.documentElement, { childList: true });
    if (document.readyState !== "complete") {
      readyStateListener = () => {
        if (document.readyState === "complete") {
          stopDarkThemeDetector();
          runCheck(callback);
        }
      };
      document.addEventListener("readystatechange", readyStateListener);
    }
  }
  function stopDarkThemeDetector() {
    if (observer) {
      observer.disconnect();
      observer = null;
    }
    if (readyStateListener) {
      document.removeEventListener("readystatechange", readyStateListener);
      readyStateListener = null;
    }
    stopDetectingUsingHint();
  }
  var hintTargetObserver;
  var hintMatchObserver;
  function detectUsingHint(hint, success) {
    stopDetectingUsingHint();
    const matchSelector = (hint.match || []).join(", ");
    function checkMatch(target2) {
      if (target2.matches?.(matchSelector)) {
        stopDetectingUsingHint();
        success();
        return true;
      }
      return false;
    }
    function setupMatchObserver(target2) {
      hintMatchObserver?.disconnect();
      if (checkMatch(target2)) {
        return;
      }
      hintMatchObserver = new MutationObserver(() => checkMatch(target2));
      hintMatchObserver.observe(target2, { attributes: true });
    }
    const target = document.querySelector(hint.target);
    if (target) {
      setupMatchObserver(target);
    } else {
      hintTargetObserver = new MutationObserver((mutations) => {
        const handledTargets = /* @__PURE__ */ new Set();
        for (const mutation of mutations) {
          if (handledTargets.has(mutation.target)) {
            continue;
          }
          handledTargets.add(mutation.target);
          if (mutation.target instanceof Element) {
            const target2 = mutation.target.querySelector(hint.target);
            if (target2) {
              hintTargetObserver.disconnect();
              setupMatchObserver(target2);
              break;
            }
          }
        }
      });
      hintTargetObserver.observe(document.documentElement, { childList: true, subtree: true });
    }
  }
  function stopDetectingUsingHint() {
    hintTargetObserver?.disconnect();
    hintMatchObserver?.disconnect();
  }
  return __toCommonJS(detector_exports);
})();
