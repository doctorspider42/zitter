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
      <header><div><h1 id="heading"></h1><p id="profile" class="muted"></p></div>
        <label class="switch-label"><input id="developer" type="checkbox" role="switch"><span></span></label>
      </header>
      <div id="tools" class="tools" hidden>
        <button type="button" id="install"></button>
      </div>
      <div class="filters">
        <label class="search"><input id="search" type="search"></label>
        <label id="auto-pin-label" class="switch-label" hidden>
          <input id="auto-pin" type="checkbox" role="switch"><span></span>
        </label>
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
