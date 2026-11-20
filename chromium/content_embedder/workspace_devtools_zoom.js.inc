uR"ZOOM(
(() => {
  let indicator;
  DevToolsAPI.zitterShowZoom = async (percent, canZoomOut, canZoomIn) => {
    indicator ??= import('devtools://devtools/bundled/ui/legacy/legacy.js').then(
        createIndicator);
    (await indicator)(percent, canZoomOut, canZoomIn);
  };

  function createIndicator({ActionRegistry, InspectorView}) {
    const inspector = InspectorView.InspectorView.instance();
    const header = inspector.tabbedPane.headerElement();
    const actions = ActionRegistry.ActionRegistry.instance();
    const host = document.createElement('div');
    const shadow = host.attachShadow({mode: 'open'});
    const stylesheet = new CSSStyleSheet();
    stylesheet.replaceSync(`
      :host {
        position: absolute;
        z-index: 1;
        inset-inline-end: var(--zoom-gap);
        max-inline-size: calc(100% - 2 * var(--zoom-gap));
        color: var(--sys-color-on-surface);
        font: var(--sys-typescale-body4-regular);
      }
      .indicator { zoom: var(--indicator-scale); }
      button, .controls {
        background: var(--sys-color-cdt-base-container);
        border: 1px solid var(--sys-color-divider);
        border-radius: 8px;
      }
      button {
        color: inherit;
        font: inherit;
        min-block-size: 24px;
        padding-inline: 8px;
      }
      button:not(:disabled):hover {
        background: var(--sys-color-state-hover-on-subtle);
      }
      button:disabled { opacity: .55; }
      button:focus-visible {
        outline: 2px solid var(--sys-color-primary);
        outline-offset: 1px;
      }
      .chip {
        display: flex;
        align-items: center;
        gap: 6px;
        margin-inline-start: auto;
        box-shadow: var(--sys-elevation-level2);
        font-variant-numeric: tabular-nums;
      }
      .magnifier {
        position: relative;
        box-sizing: border-box;
        width: 9px;
        height: 9px;
        border: 1px solid currentColor;
        border-radius: 50%;
        margin-inline-end: 2px;
      }
      .magnifier::after {
        content: '';
        position: absolute;
        right: -3px;
        bottom: -2px;
        width: 4px;
        border-top: 1px solid currentColor;
        transform: rotate(45deg);
      }
      .controls {
        display: grid;
        gap: 8px;
        width: max-content;
        max-width: 100%;
        box-sizing: border-box;
        margin-top: 4px;
        padding: 8px;
        box-shadow: var(--sys-elevation-level2);
      }
      .steps {
        display: flex;
        justify-content: space-between;
        align-items: center;
        gap: 8px;
        font-variant-numeric: tabular-nums;
      }
      .announcement {
        position: absolute;
        width: 1px;
        height: 1px;
        overflow: hidden;
        clip-path: inset(50%);
        white-space: nowrap;
      }
      [hidden] { display: none; }
    `);
    shadow.adoptedStyleSheets = [stylesheet];

    const chip = document.createElement('button');
    chip.type = 'button';
    chip.className = 'chip';
    chip.hidden = true;
    chip.setAttribute('aria-expanded', 'false');
    chip.setAttribute('aria-controls', 'zoom-controls');
    const icon = document.createElement('span');
    icon.className = 'magnifier';
    icon.setAttribute('aria-hidden', 'true');
    const value = document.createElement('span');
    chip.append(icon, value);

    const controls = document.createElement('div');
    controls.id = 'zoom-controls';
    controls.className = 'controls';
    controls.hidden = true;
    const resetAction = actions.getAction('main.zoom-reset');
    controls.setAttribute('role', 'group');
    controls.setAttribute('aria-label', resetAction.title());
    const steps = document.createElement('div');
    steps.className = 'steps';
    const current = document.createElement('span');
    const zoomOut = zoomButton('main.zoom-out', '−');
    const zoomIn = zoomButton('main.zoom-in', '+');
    const reset = zoomButton('main.zoom-reset', resetAction.title());
    steps.append(zoomOut, current, zoomIn);
    controls.append(steps, reset);

    const announcement = document.createElement('div');
    announcement.className = 'announcement';
    announcement.setAttribute('role', 'status');
    const content = document.createElement('div');
    content.className = 'indicator';
    content.append(chip, controls, announcement);
    shadow.append(content);
    inspector.element.append(host);

    let gap = 8;
    function position() {
      const offset = header.getBoundingClientRect().bottom -
          inspector.element.getBoundingClientRect().top;
      host.style.insetBlockStart = `${offset + gap}px`;
    }
    new ResizeObserver(position).observe(header);

    let timer;
    let recent = false;
    let atDefaultZoom = true;
    function hideIfIdle() {
      if (atDefaultZoom && !recent && controls.hidden && !host.matches(':hover') &&
          !shadow.activeElement) {
        chip.hidden = true;
      }
    }

    function setOpen(open) {
      controls.hidden = !open;
      chip.setAttribute('aria-expanded', String(open));
      hideIfIdle();
    }
    chip.addEventListener('click', () => setOpen(controls.hidden));
    host.addEventListener('pointerleave', hideIfIdle);
    host.addEventListener('focusout', () => queueMicrotask(hideIfIdle));
    host.addEventListener('keydown', event => {
      if (event.key === 'Escape' && !controls.hidden) {
        event.preventDefault();
        event.stopPropagation();
        chip.focus();
        setOpen(false);
      }
    });
    document.addEventListener('pointerdown', event => {
      if (!controls.hidden && !event.composedPath().includes(host))
        setOpen(false);
    }, true);
    window.addEventListener('blur', () => setOpen(false));

    function zoomButton(id, text) {
      const action = actions.getAction(id);
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = text;
      button.setAttribute('aria-label', action.title());
      button.addEventListener('click', () => {
        if (action === resetAction) {
          chip.focus();
          setOpen(false);
        }
        void action.execute();
      });
      return button;
    }

    return (percent, canZoomOut, canZoomIn) => {
      const label = `${percent}%`;
      // Keep the controls readable independently of the frontend's zoom.
      const scale = 100 / percent;
      gap = 8 * scale;
      host.style.setProperty('--indicator-scale', scale);
      host.style.setProperty('--zoom-gap', `${gap}px`);
      value.textContent = label;
      current.textContent = label;
      announcement.textContent = `DevTools ${label}`;
      chip.setAttribute('aria-label', announcement.textContent);
      zoomOut.disabled = !canZoomOut;
      zoomIn.disabled = !canZoomIn;
      atDefaultZoom = percent === 100;
      reset.disabled = atDefaultZoom;
      recent = atDefaultZoom;
      position();
      chip.hidden = false;
      clearTimeout(timer);
      if (recent) {
        timer = setTimeout(() => {
          recent = false;
          hideIfIdle();
        }, 2600);
      }
    };
  }
})();
)ZOOM"
