R"MANAGER(
'use strict';

const $ = id => document.getElementById(id);
const glyphs = {
  link: 'M10 13a4 4 0 0 0 6 0l4-4a4 4 0 0 0-6-6l-2 2m2 6a4 4 0 0 0-6 0l-4 4a4 4 0 0 0 6 6l2-2',
  puzzle: 'M4 5h5a3 3 0 1 1 6 0h5v5a3 3 0 1 0 0 6v4H4v-5a3 3 0 1 0 0-6z',
  keyboard: 'M3 5h18v14H3zM7 9h.1m4 0h.1m4 0h.1m3 0h.1M7 13h.1m4 0h.1m4 0h.1m3 0h.1M8 16h8',
  store: 'M4 8h16l-1 12H5zM8 8V7a4 4 0 0 1 8 0v1',
  search: 'M15 15l5 5M17 11a6 6 0 1 1-12 0 6 6 0 0 1 12 0z',
  pin: 'M9 4h6l-1 6 4 4H6l4-4zM12 14v6',
  trash: 'M5 7h14M9 7V4h6v3M7 7l1 13h8l1-13M10 11v5m4-5v5',
  back: 'M19 12H5m6-6-6 6 6 6',
  arrow: 'M6 18 18 6M8 6h10v10',
  package: 'M3 7l9-4 9 4v10l-9 4-9-4zm0 0 9 4 9-4m-9 4v10',
};

function element(tag, className = '', text = '') {
  const node = document.createElement(tag);
  node.className = className;
  node.textContent = text;
  return node;
}

function icon(name) {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('aria-hidden', 'true');
  const path = document.createElementNS(ns, 'path');
  path.setAttribute('d', glyphs[name]);
  svg.append(path);
  return svg;
}

function labelButton(node, label, glyph) {
  node.replaceChildren();
  if (glyph) node.append(icon(glyph));
  node.append(label);
}

let state = {items: []};
let view = 'list';
let selected = '';
let serial = 0;
let pending = null;
let localError = '';
const t = (polish, english) => state.english ? english : polish;

function request(op, fields = {}) {
  if (pending) return;
  const id = ++serial;
  pending = {id, op};
  chrome.send('zitterExtensionsRequest', [id, {op, ...fields}]);
}

window.zitterExtensionsReply = (id, reply) => {
  if (pending?.id !== id) return;
  const {op} = pending;
  pending = null;
  if (!reply.colors) return;
  if (op !== 'read') {
    localError = reply.ok ? '' : op === 'installStore'
        ? t('Sprawdź link z Chrome Web Store lub spróbuj za chwilę.',
            'Check the Chrome Web Store link or try again in a moment.')
        : t('Nie udało się zapisać zmiany lub skrót jest zajęty.',
                                   'The change could not be saved or the shortcut is already in use.');
  }
  delete reply.ok;
  const changed = JSON.stringify(reply) !== JSON.stringify(state);
  state = reply;
  if (changed || op !== 'read') {
    const focused = document.activeElement?.dataset.focus;
    render();
    if (focused) {
      const control = [...document.querySelectorAll('[data-focus]')]
          .find(node => node.dataset.focus === focused && !node.closest('[hidden]'));
      control?.focus();
    }
  }
};

function action(text, fn, glyph, className = '') {
  const button = element('button', className);
  button.type = 'button';
  labelButton(button, text, glyph);
  button.addEventListener('click', fn);
  return button;
}

function toggle(label, checked, enabled, fn) {
  const control = element('label', 'switch-label');
  const input = element('input');
  input.type = 'checkbox';
  input.role = 'switch';
  input.checked = checked;
  input.disabled = !enabled;
  input.addEventListener('change', () => fn(input.checked));
  control.append(input, element('span', '', label));
  return control;
}

function extensionIcon(item) {
  const tile = element('span', 'tile');
  if (item.icon) {
    const img = element('img');
    img.alt = '';
    img.src = item.icon;
    tile.append(img);
  } else {
    tile.append(icon('puzzle'));
  }
  return tile;
}

function identity(item, heading = 'strong') {
  const row = element('div', 'identity');
  const copy = element('div');
  const showVersion = heading === 'h1' || $('developer').checked;
  copy.append(element(heading, '', item.name),
      element('p', 'muted', [item.description, showVersion ? item.version : '']
          .filter(Boolean).join(' · ')));
  row.append(extensionIcon(item), copy);
  return row;
}

function enable(item) {
  const text = item.requestedEnabled ? t('Włączone', 'Enabled') : t('Wyłączone', 'Disabled');
  const control = toggle(text, item.requestedEnabled,
      item.editable && item.requestedEnabled === item.enabled,
      value => request('enable', {id: item.id, value}));
  control.firstChild.setAttribute('aria-label', item.name + ' · ' + text);
  control.firstChild.dataset.focus = 'enable-' + item.id;
  return control;
}

function pin(item) {
  const text = item.pinned ? t('Odepnij od paska', 'Unpin from toolbar')
                           : t('Przypnij do paska', 'Pin to toolbar');
  const button = action('', () => request('pin', {id: item.id, value: !item.pinned}), 'pin');
  button.setAttribute('aria-label', text + ' · ' + item.name);
  button.title = text;
  button.setAttribute('aria-pressed', item.pinned);
  button.dataset.focus = 'pin-' + item.id;
  button.disabled = !item.editable || (!item.enabled && !item.pinned);
  return button;
}

function remove(item, withText) {
  const text = t('Usuń rozszerzenie', 'Remove extension');
  const button = action(withText ? text : '', () => request('remove', {id: item.id}),
                        'trash', withText ? 'outline' : '');
  button.setAttribute('aria-label', text + ' · ' + item.name);
  button.title = text;
  button.disabled = !item.editable;
  return button;
}

function renderList() {
  const query = $('search').value.toLocaleLowerCase().trim();
  const items = state.items.filter(item =>
      (item.name + ' ' + item.description).toLocaleLowerCase().includes(query));
  $('items').replaceChildren();
  for (const item of items) {
    const card = element('article', 'extension-card');
    const top = element('div', 'card-top');
    top.append(extensionIcon(item));
    if (item.canPin) top.append(pin(item));
    const copy = element('div', 'card-copy');
    copy.append(element('h3', '', item.name),
        element('p', 'muted', [item.description, $('developer').checked ? item.version : '']
            .filter(Boolean).join(' · ')));
    const actions = element('div', 'card-actions');
    actions.append(action(t('Szczegóły', 'Details'), () => {
      selected = item.id;
      navigate('detail');
    }, 'arrow'), remove(item, false));
    card.append(top, copy, enable(item), actions);
    $('items').append(card);
  }
  if (!items.length) {
    const text = query ? t('Brak wyników.', 'No results.')
                       : t('Brak rozszerzeń w tym profilu.', 'This profile has no extensions.');
    $('items').append(element('p', 'empty', text));
  }
}

function detailRow(label, value) {
  const row = element('div', 'row');
  row.append(element('span', 'detail-label', label), element('span', 'detail-value muted', value));
  return row;
}

function renderDetail() {
  const item = state.items.find(item => item.id === selected);
  if (!item) {
    navigate('list');
    return;
  }
  const head = element('header');
  head.append(identity(item, 'h1'), enable(item));
  const permissions = element('h2', '', t('Dostęp i uprawnienia', 'Site access and permissions'));
  const noAccess = t('Brak', 'None');
  const actions = element('div', 'row');
  if (item.canPin) actions.append(pin(item));
  if (item.options) {
    actions.append(action(t('Opcje rozszerzenia', 'Extension options'),
        () => request('options', {id: item.id}), 'arrow', 'outline'));
  }
  actions.append(remove(item, true));
  const nodes = [
    head, permissions,
    detailRow(t('Dostęp do witryn', 'Site access'), item.hosts.join(', ') || noAccess),
    detailRow(t('Uprawnienia', 'Permissions'), item.permissions.join(', ') || noAccess),
  ];
  if (typeof item.scripts === 'boolean') {
    const row = element('div', 'row');
    row.append(toggle(t('Zezwalaj na skrypty użytkownika', 'Allow user scripts'), item.scripts,
        item.enabled && item.editable, value => request('scripts', {id: item.id, value})));
    nodes.push(row);
  }
  nodes.push(element('h2', '', t('Informacje', 'Information')),
      detailRow(t('Źródło', 'Source'), item.webStore ? 'Chrome Web Store' : t('Pakiet CRX', 'CRX package')),
      detailRow(t('Wersja', 'Version'), item.version),
      detailRow(t('Identyfikator', 'ID'), item.id), actions);
  $('detail-body').replaceChildren(...nodes);
}

function captureShortcut(event, item, command) {
  if (event.key === 'Tab' || event.key === 'Escape') return;
  event.preventDefault();
  if (event.key === 'Backspace' || event.key === 'Delete') {
    request('shortcut', {id: item.id, command: command.name, key: ''});
    return;
  }
  const mods = [];
  if (event.ctrlKey) mods.push('Ctrl');
  if (event.altKey) mods.push('Alt');
  if (event.metaKey) mods.push('Command');
  if (event.shiftKey) mods.push('Shift');
  const physical = /^(?:Key([A-Z])|Digit([0-9]))$/.exec(event.code);
  const key = physical ? physical[1] || physical[2]
      : event.key === ' ' ? 'Space' : event.key.replace(/^Arrow/, '');
  if (!mods.length || !/^(?:[a-z0-9]|Space|Home|End|PageUp|PageDown|Left|Right|Up|Down|Insert)$/i.test(key))
    return;
  mods.push(key.length === 1 ? key.toUpperCase() : key);
  request('shortcut', {id: item.id, command: command.name, key: mods.join('+')});
}

function renderShortcuts() {
  const rows = [];
  for (const item of state.items.filter(item => item.enabled)) {
    for (const command of item.commands) {
      const row = element('div', 'row');
      row.append(identity({...item, description: command.description ||
          t('Aktywuj rozszerzenie', 'Activate extension')}));
      const input = element('input', 'command');
      input.type = 'text';
      input.readOnly = true;
      input.disabled = command.global || !item.editable;
      input.placeholder = command.global ? t('Globalny', 'Global')
                                         : t('Ustaw skrót', 'Set shortcut');
      input.value = command.shortcut;
      input.dataset.focus = item.id + ':' + command.name;
      input.setAttribute('aria-label', item.name + ' · ' + (command.description || input.placeholder));
      input.addEventListener('keydown', event => captureShortcut(event, item, command));
      row.append(input);
      rows.push(row);
    }
  }
  $('commands').replaceChildren(...(rows.length ? rows : [
    element('p', 'empty', t('Aktywne rozszerzenia nie udostępniają skrótów.',
                            'Active extensions do not expose any shortcuts.')),
  ]));
}

function render() {
  for (const [key, value] of Object.entries(state.colors || {}))
    document.documentElement.style.setProperty('--' + key, value);
  document.documentElement.style.colorScheme = state.dark ? 'dark' : 'light';
  document.documentElement.lang = state.english ? 'en' : 'pl';
  const title = t('Rozszerzenia', 'Extensions');
  document.title = title;
  $('heading').textContent = title;
  $('installed-title').textContent = t('Zainstalowane', 'Installed');
  $('profile').textContent = state.profile ? t('Profil ', 'Profile ') + state.profile : '';
  const shortcuts = t('Skróty klawiszowe', 'Keyboard shortcuts');
  $('shortcuts-title').textContent = shortcuts;
  labelButton(document.querySelector('[data-view=list]'), title, 'puzzle');
  labelButton(document.querySelector('[data-view=shortcuts]'), shortcuts, 'keyboard');
  labelButton($('store'), 'Chrome Web Store', 'store');
  labelButton($('back'), title, 'back');
  labelButton($('install'), t('Zainstaluj lub zaktualizuj z CRX', 'Install or update from CRX'), 'package');
  $('developer').nextElementSibling.textContent = t('Tryb deweloperski', 'Developer mode');
  $('tools').hidden = !$('developer').checked || !state.canInstall;
  $('store-install').hidden = !state.canInstall;
  $('store-add').disabled = !!state.installBusy;
  $('store-link').placeholder = t('Wklej link z Chrome Web Store', 'Paste a Chrome Web Store link');
  $('store-link').setAttribute('aria-label', $('store-link').placeholder);
  labelButton($('store-add'), t('Dodaj ze sklepu', 'Add from Web Store'), 'store');
  $('auto-pin-label').hidden = typeof state.autoPin !== 'boolean';
  $('auto-pin').checked = !!state.autoPin;
  $('auto-pin').nextElementSibling.textContent = t('Przypinaj nowe do paska', 'Pin new extensions to toolbar');
  $('search').placeholder = t('Szukaj rozszerzeń…', 'Search extensions…');
  $('search').setAttribute('aria-label', $('search').placeholder);
  const message = state.status || localError;
  $('status').textContent = message;
  $('status').hidden = !message;
  $('status').dataset.error = !!(state.error || localError);
  if (view === 'list') renderList();
  if (view === 'detail') renderDetail();
  if (view === 'shortcuts') renderShortcuts();
}

function navigate(next) {
  view = next;
  for (const section of document.querySelectorAll('main > section'))
    section.hidden = section.id !== view;
  for (const button of document.querySelectorAll('nav [data-view]')) {
    if (button.dataset.view === (view === 'detail' ? 'list' : view))
      button.setAttribute('aria-current', 'page');
    else
      button.removeAttribute('aria-current');
  }
  render();
}

for (const button of document.querySelectorAll('nav [data-view]'))
  button.addEventListener('click', () => navigate(button.dataset.view));
$('back').addEventListener('click', () => navigate('list'));
$('search').parentElement.prepend(icon('search'));
$('store-link').parentElement.prepend(icon('link'));
$('search').addEventListener('input', renderList);
$('developer').addEventListener('change', render);
$('install').addEventListener('click', () => request('install'));
$('store-install').addEventListener('submit', event => {
  event.preventDefault();
  request('installStore', {listing: $('store-link').value});
});
$('store').addEventListener('click', () => request('store'));
$('auto-pin').addEventListener('change', () => request('autoPin', {value: $('auto-pin').checked}));
request('read');
setInterval(() => {
  if (document.visibilityState === 'visible') request('read');
}, 1200);
)MANAGER"
