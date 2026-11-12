R"MANAGER(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="stylesheet" href="manager.css">
<script defer src="manager.js"></script>
<title>Extensions</title>
</head>
<body>
<div class="layout">
  <nav>
    <button type="button" data-view="list" aria-current="page"></button>
    <button type="button" data-view="shortcuts"></button>
    <button type="button" id="store"></button>
  </nav>
  <main>
    <section id="list">
      <div class="hero">
        <div class="hero-content">
          <p id="profile" class="muted"></p>
          <h1><span id="heading"></span><span class="hero-dot" aria-hidden="true">.</span></h1>
        </div>
        <span class="hero-mark" aria-hidden="true">|z⟩</span>
      </div>
      <div class="library-heading">
        <h2 id="installed-title"></h2>
      </div>
      <div id="tools" class="tools" hidden>
        <button type="button" id="install"></button>
      </div>
      <div class="filters">
        <label class="search"><input id="search" type="search"></label>
        <div class="library-switches">
        <label class="switch-label"><input id="developer" type="checkbox" role="switch"><span></span></label>
        <label id="auto-pin-label" class="switch-label" hidden>
          <input id="auto-pin" type="checkbox" role="switch"><span></span>
        </label>
        </div>
      </div>
      <div id="items"></div>
    </section>
    <section id="detail" hidden>
      <button type="button" class="quiet" id="back"></button>
      <div id="detail-body"></div>
    </section>
    <section id="shortcuts" hidden>
      <h1 id="shortcuts-title"></h1>
      <div id="commands"></div>
    </section>
    <p id="status" role="status" hidden></p>
  </main>
</div>
</body>
</html>
)MANAGER"
