# Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
# Windows counterpart of build_app.sh: install the overlay into .chromium\src and
# build zitter.exe with the tests that cover the UI, in out\ZitterWin.
# Needs the toolchain from docs/windows-port.md (Phase 0); set vs2026_install
# when Visual Studio sits under Program Files (x86).
$ErrorActionPreference = 'Stop'
$root = (git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
$src = Join-Path $root '.chromium\src'
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
$env:PATH = "$(Join-Path $root '.chromium\depot_tools');$env:PATH"

python (Join-Path $root 'tools\install_chromium_overlay.py') $src
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Push-Location $src
try {
  if (-not (Test-Path 'out\ZitterWin\args.gn')) {
    New-Item -ItemType Directory -Force 'out\ZitterWin' | Out-Null
    Copy-Item (Join-Path $root 'chromium\windows.args.gn') 'out\ZitterWin\args.gn'
  }
  $ErrorActionPreference = 'Continue'  # ninja reports progress on stderr
  & autoninja -C out\ZitterWin zitter:zitter zitter:sidebar_scene_unittests zitter:editor_text_layout_unittests
  exit $LASTEXITCODE
} finally {
  Pop-Location
}
