# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
# Windows counterpart of build_app.sh: stage the Content embedder into
# .chromium\src and build zitter.exe (//zitter_content:zitter_content) with the
# tests that cover the UI, in out\ZitterWinContent. There is no package: the
# build directory, with the design fonts in its Fonts\, is the app.
# Needs the toolchain from docs/windows-port.md (Phase 0); set vs2026_install
# when Visual Studio sits under Program Files (x86).
$ErrorActionPreference = 'Stop'
$root = (git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
$src = Join-Path $root '.chromium\src'
$out = 'out\ZitterWinContent'
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
$env:PATH = "$(Join-Path $root '.chromium\depot_tools');$env:PATH"

python (Join-Path $root 'tools\stage_content_embedder.py') $src
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Push-Location $src
try {
  if (-not (Test-Path "$out\args.gn")) {
    New-Item -ItemType Directory -Force $out | Out-Null
    Copy-Item (Join-Path $root 'chromium\windows-release.args.gn') "$out\args.gn"
  }
  $ErrorActionPreference = 'Continue'  # gn and ninja report progress on stderr
  & gn gen $out --root-target=//zitter_content:zitter_content
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
  & autoninja -C $out zitter_content sidebar_scene_unittests zitter:editor_text_layout_unittests
  exit $LASTEXITCODE
} finally {
  Pop-Location
}
