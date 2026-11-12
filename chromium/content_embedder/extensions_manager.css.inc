R"MANAGER(
:root {
  color-scheme: light dark;
  --frame: light-dark(#ebe8e1, #0c0b11);
  --sidebar: light-dark(#f2efe9, #17131f);
  --surface: light-dark(#faf9f6, #1e1b28);
  --hover: light-dark(#eee8f6, #302641);
  --line: light-dark(#e4dceb, #392e47);
  --edge: light-dark(#90849e, #82758f);
  --ink: light-dark(#252032, #f2eef8);
  --muted: light-dark(#62596c, #b9afc6);
  --accent: light-dark(#6644a5, #d1b4f5);
  --on-accent: light-dark(#fff, #251732);
  --accent-ink: var(--accent);
  --danger: light-dark(#a13424, #ffad91);
  background: var(--frame);
  color: var(--ink);
  font: 14px/1.45 system-ui, sans-serif;
}
* { box-sizing: border-box; }
body { margin: 0; }
[hidden] { display: none !important; }
.layout { min-height: 100vh; background: var(--surface); }
nav {
  display: flex; flex-wrap: wrap; align-items: center; gap: 4px;
  padding: 7px clamp(12px, 3vw, 32px);
  background: var(--sidebar); border-bottom: 1px solid var(--line);
}
nav button { justify-content: flex-start; text-align: start; }
nav [aria-current=page] { background: var(--surface); color: var(--accent-ink); }
main { min-width: 0; padding: clamp(18px, 4vw, 48px); background: var(--surface); }
main > * { max-width: 1050px; margin-inline: auto; }
header, .filters, .row, .identity, .tools, .library-heading {
  display: flex; align-items: center; gap: 12px; flex-wrap: wrap;
}
header, .filters, .library-heading { justify-content: space-between; }
header { margin-bottom: 20px; }
.filters, .tools { padding: 10px 0; border-bottom: 1px solid var(--line); }
.search { display: flex; flex: 1; min-width: 160px; gap: 8px; align-items: center; }
.search input { flex: 1; min-width: 0; }
h1, h2, h3, p { margin: 0; overflow-wrap: anywhere; }
h1 { font-size: 26px; letter-spacing: -.025em; }
h2 { font-size: 15px; margin-top: 20px; }
.muted { color: var(--muted); }
button, input, select { font: inherit; color: inherit; }
button {
  display: inline-flex; align-items: center; justify-content: center; gap: 7px;
  min-height: 34px; border: 1px solid transparent; border-radius: 8px;
  padding: 4px 9px; background: transparent; cursor: pointer;
}
button:hover:not(:disabled), .card-top [aria-pressed=true] {
  background: var(--hover); color: var(--accent-ink);
}
button:disabled, input:disabled { opacity: .6; cursor: default; }
button.outline, input:not([type=checkbox]), select {
  border: 1px solid var(--edge); border-radius: 8px; background: var(--surface);
}
input:not([type=checkbox]), select { padding: 6px 9px; min-height: 34px; }
:focus-visible { outline: 2px solid var(--accent-ink); outline-offset: 2px; }
.row { padding: 12px 0; border-bottom: 1px solid var(--line); }
.identity { flex: 1 1 200px; flex-wrap: nowrap; min-width: 0; }
.identity > div { min-width: 0; overflow-wrap: anywhere; }
.tile {
  display: grid; flex: 0 0 34px; width: 34px; height: 34px; place-items: center;
  border-radius: 8px; background: var(--hover); color: var(--accent-ink);
}
.tile img { width: 24px; height: 24px; object-fit: contain; }
svg { flex: 0 0 16px; width: 16px; height: 16px; stroke: currentColor; fill: none;
  stroke-width: 1.6; stroke-linecap: round; stroke-linejoin: round; }
.switch-label { display: inline-flex; align-items: center; gap: 7px; cursor: pointer; }
.switch-label input {
  appearance: none; width: 32px; height: 19px; margin: 0; border-radius: 12px;
  border: 1px solid var(--edge); background: transparent; position: relative; cursor: inherit;
}
.switch-label input::before {
  content: ""; position: absolute; inset: 2px auto auto 2px;
  width: 13px; height: 13px; border-radius: 50%; background: var(--muted);
}
.switch-label input:checked { background: var(--accent); border-color: var(--accent); }
.switch-label input:checked::before { left: 15px; background: var(--on-accent); }
.quiet { margin-inline-start: -9px; margin-bottom: 10px; }
#status:not([hidden]) { margin-top: 16px; color: var(--muted); }
#status[data-error=true] { color: var(--danger); }
.empty { padding: 18px 0; color: var(--muted); }
.command { margin-inline-start: auto; width: min(150px, 100%); }
.detail-label { min-width: 105px; font-weight: 600; }
.detail-value { flex: 1 1 220px; overflow-wrap: anywhere; }
.hero {
  display: grid; grid-template-columns: minmax(0, 3fr) minmax(0, 1fr);
  align-items: center; overflow: hidden; isolation: isolate;
  min-height: 126px; padding: clamp(18px, 3vw, 32px);
  border-radius: 14px; background: var(--sidebar);
}
.hero-content { min-width: 0; z-index: 1; }
.hero h1 {
  margin: 5px 0 0;
  font: italic 500 clamp(34px, 7vw, 76px)/1 Georgia, serif;
  letter-spacing: -.067em;
}
.hero-dot, .hero-mark { color: var(--accent-ink); }
.hero-mark {
  place-self: center; pointer-events: none; white-space: nowrap;
  font: italic 400 clamp(56px, 9vw, 100px)/.85 Georgia, serif;
  letter-spacing: -.12em; opacity: .52;
}
.library-heading { margin-top: 24px; }
.library-heading h2 { margin: 0; font-size: 18px; }
.library-switches { display: flex; flex-wrap: wrap; align-items: center; gap: 8px 18px; }
.filters { border: 0; margin: 3px 0 8px; }
#items {
  display: grid; grid-template-columns: repeat(auto-fill, minmax(min(100%, 205px), 1fr));
  gap: 12px;
}
.extension-card {
  display: flex; flex-direction: column; min-width: 0; gap: 11px;
  padding: 13px; background: var(--sidebar); border-radius: 12px;
}
.card-top, .card-actions { display: flex; align-items: center; justify-content: space-between; }
.card-top .tile { width: 43px; height: 43px; flex-basis: 43px; }
.card-top .tile img { width: 27px; height: 27px; }
.card-copy { flex: 1; }
.card-copy h3 { margin-bottom: 3px; font-size: 15px; }
.card-actions { border-top: 1px solid var(--line); padding-top: 7px; }
.card-actions button:last-child:hover:not(:disabled) { color: var(--danger); }
@media (max-width: 600px) {
  main { padding: 16px; }
  .hero { grid-template-columns: minmax(0, 1fr); }
  .hero-mark { display: none; }
}
)MANAGER"
