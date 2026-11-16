// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// What each zitter-dev job runs. A job is one script (zsh on macOS, PowerShell
// on Windows) that helper/zjob.py starts detached in its own process group, so
// it outlives a reload of the mod and can be stopped as a whole.

import type { JobKind } from '../types'

export type Platform = 'macos' | 'windows'

/** The output directory the dev build uses on each platform. */
export const OUT_DIR: Record<Platform, string> = { macos: 'out/ZitterOptimized', windows: 'out/ZitterWin' }

export type KindInfo = {
  title: string
  /** One line for the command menu and the model's tool description. */
  description: string
  /** Whether it stages or builds in the shared build directory. */
  usesBuildDir: boolean
  /** The platforms it runs on; all when absent. */
  only?: Platform
}

export const KINDS: Record<JobKind, KindInfo> = {
  build: { title: 'Build', description: 'stage //zitter, build and package build/ZitterRustMetal.app (./build.sh)', usesBuildDir: true },
  run: { title: 'Build & run', description: 'build, then relaunch the app with its isolated profile', usesBuildDir: true },
  launch: { title: 'Launch', description: 'relaunch the last packaged app without building', usesBuildDir: false },
  full: { title: 'Build app + tests', description: 'build the app, zitter_prototype and the Rust tests (verify-ui build_app.sh)', usesBuildDir: true },
  test: { title: 'Tests', description: 'build and run sidebar_scene_unittests, then the Python tests', usesBuildDir: true },
  uitest: { title: 'UI tests', description: 'build zitter_prototype and run the browser tests one --gtest_filter names (at most 25)', usesBuildDir: true },
  content: { title: 'Content build', description: 'build and package the Content lab (./build.sh --content; macOS)', usesBuildDir: true, only: 'macos' },
  'content-run': { title: 'Content run', description: 'build the Content lab and run it with temporary profiles (macOS)', usesBuildDir: true, only: 'macos' },
}

export const KIND_NAMES = Object.keys(KINDS) as JobKind[]

export const kindsFor = (platform: Platform) => KIND_NAMES.filter(k => (KINDS[k].only ?? platform) === platform)

/** A single-quoted shell word. */
export const sq = (s: string) => `'${s.replace(/'/g, `'\\''`)}'`

const PRELUDE = [
  'set -o pipefail',
  'PY=.chromium/depot_tools/python-bin/python3; [[ -x $PY ]] || PY=python3',
  'NINJA=(.chromium/src/third_party/ninja/ninja -C .chromium/src/out/ZitterOptimized -j ${ZITTER_BUILD_JOBS:-6})',
  'LAUNCH=(python3 .claude/skills/verify-ui/scripts/ui.py launch)',
].join('\n')

const MAX_UI_TESTS = 25

/** The body of a job's script, run from the repository root. */
export function body(kind: JobKind, filter?: string): string {
  switch (kind) {
    case 'build':
      return './build.sh'
    case 'run':
      return './build.sh && echo "== Relaunching the app" && $LAUNCH'
    case 'launch':
      return '$LAUNCH'
    case 'full':
      return 'zsh .claude/skills/verify-ui/scripts/build_app.sh'
    case 'test':
      return [
        '$PY tools/install_chromium_overlay.py .chromium/src || exit 1',
        '$NINJA sidebar_scene_unittests || exit 1',
        'r=0',
        'echo "== Rust: sidebar_scene_unittests"',
        '.chromium/src/out/ZitterOptimized/sidebar_scene_unittests || r=1',
        'echo "== Python: tests/test_*.py"',
        "python3 -m unittest discover -s tests -p 'test_*.py' || r=1",
        'exit $r',
      ].join('\n')
    case 'uitest':
      return [
        `F=${sq(filter ?? '')}`,
        '[[ -n $F && $F != "*" ]] || { echo "Name the tests with a --gtest_filter pattern."; exit 2 }',
        '$PY tools/install_chromium_overlay.py .chromium/src || exit 1',
        '$NINJA zitter_prototype || exit 1',
        'B=.chromium/src/out/ZitterOptimized/zitter_prototype',
        'n=$($B --gtest_filter="$F" --gtest_list_tests | grep -c "^  ")',
        'echo "== $n browser tests match $F"',
        '(( n > 0 )) || { echo "No test matches."; exit 1 }',
        `(( n <= ${MAX_UI_TESTS} )) || { echo "Too broad: $n tests would take over the screen for long. Narrow the filter."; exit 3 }`,
        '$B --gtest_filter="$F"',
      ].join('\n')
    case 'content':
      return './build.sh --content'
    case 'content-run':
      return './build.sh --content --run'
  }
}

/** The whole script of a job: it writes `<exit code> <end time>` when it ends. */
export function script(platform: Platform, root: string, exitFile: string, title: string, kind: JobKind, filter?: string): string {
  if (platform === 'windows') return windowsScript(root, exitFile, title, kind, filter)
  return [
    '#!/bin/zsh',
    `cd -- ${sq(root)} || { print -r -- "1 $(date +%s)" > ${sq(exitFile)}; exit 1 }`,
    PRELUDE,
    `print -r -- "== zitter-dev: ${title.replace(/["$`\\]/g, '')} ($(date '+%H:%M:%S'), $(git rev-parse --short HEAD))"`,
    '(',
    body(kind, filter),
    ')',
    'code=$?',
    `print -r -- "$code $(date +%s)" > ${sq(exitFile)}`,
    'print -r -- "== exit $code"',
    'exit $code',
    '',
  ].join('\n')
}

/** A single-quoted PowerShell string. */
export const psq = (s: string) => `'${s.replace(/'/g, "''")}'`

/** The body of a Windows job, in PowerShell, run from the repository root (docs/windows-port.md). */
export function windowsBody(kind: JobKind, filter?: string): string {
  const stage = 'python tools\\install_chromium_overlay.py .chromium\\src; if ($LASTEXITCODE) { exit $LASTEXITCODE }'
  const build = (targets: string) => `autoninja -C .chromium\\src\\out\\ZitterWin ${targets}; if ($LASTEXITCODE) { exit $LASTEXITCODE }`
  const launch = 'python .claude\\skills\\verify-ui\\scripts\\ui_win.py launch; exit $LASTEXITCODE'
  switch (kind) {
    case 'build':
      return [stage, build('zitter:zitter')].join('\n')
    case 'run':
      return [stage, build('zitter:zitter'), "Write-Output '== Relaunching the app'", launch].join('\n')
    case 'launch':
      return launch
    case 'full':
      return '& .claude\\skills\\verify-ui\\scripts\\build_app_win.ps1; exit $LASTEXITCODE'
    case 'test':
      return [
        stage,
        build('zitter:sidebar_scene_unittests zitter:editor_text_layout_unittests'),
        '$r = 0',
        "Write-Output '== Rust: sidebar_scene_unittests, editor_text_layout_unittests'",
        '& .chromium\\src\\out\\ZitterWin\\sidebar_scene_unittests.exe; if ($LASTEXITCODE) { $r = 1 }',
        '& .chromium\\src\\out\\ZitterWin\\editor_text_layout_unittests.exe; if ($LASTEXITCODE) { $r = 1 }',
        "Write-Output '== Python: tests/test_*.py'",
        "python -m unittest discover -s tests -p 'test_*.py'; if ($LASTEXITCODE) { $r = 1 }",
        'exit $r',
      ].join('\n')
    case 'uitest':
      return [
        `$F = ${psq(filter ?? '')}`,
        "if (-not $F -or $F -eq '*') { Write-Output 'Name the tests with a --gtest_filter pattern.'; exit 2 }",
        stage,
        build('zitter:zitter_prototype'),
        '$B = ".chromium\\src\\out\\ZitterWin\\zitter_prototype.exe"',
        '$n = @(& $B "--gtest_filter=$F" --gtest_list_tests | Where-Object { $_ -match "^  " }).Count',
        'Write-Output "== $n browser tests match $F"',
        "if ($n -eq 0) { Write-Output 'No test matches.'; exit 1 }",
        `if ($n -gt ${MAX_UI_TESTS}) { Write-Output "Too broad: $n tests would take over the screen for long. Narrow the filter."; exit 3 }`,
        '& $B "--gtest_filter=$F"; exit $LASTEXITCODE',
      ].join('\n')
    case 'content':
    case 'content-run':
      return "Write-Output 'The Content lab builds on macOS only.'; exit 2"
  }
}

function windowsScript(root: string, exitFile: string, title: string, kind: JobKind, filter?: string): string {
  // The body runs as a script block, so its `exit` ends the block and the exit file is still written.
  return [
    "$ErrorActionPreference = 'Continue'",
    "$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'",
    `Set-Location -LiteralPath ${psq(root)}`,
    "$env:PATH = \"$(Join-Path (Get-Location) '.chromium\\depot_tools');$env:PATH\"",
    `Write-Output "== zitter-dev: ${title.replace(/["$`]/g, '')} ($(Get-Date -Format HH:mm:ss), $(git rev-parse --short HEAD))"`,
    '$job = {',
    windowsBody(kind, filter),
    '}',
    '$code = 0',
    'try { & $job; $code = $LASTEXITCODE } catch { Write-Output $_; $code = 1 }',
    'if ($null -eq $code) { $code = 0 }',
    `[IO.File]::WriteAllText(${psq(exitFile)}, "$code $([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())")`,
    'Write-Output "== exit $code"',
    'exit $code',
    '',
  ].join('\r\n')
}

/** The last `[done/total]` ninja wrote, if any. */
export function ninjaProgress(text: string): { done: number; total: number } | undefined {
  let last: RegExpExecArray | undefined
  for (const m of text.matchAll(/\[(\d+)\/(\d+)\]/g)) last = m as RegExpExecArray
  if (!last) return
  const done = Number(last[1])
  const total = Number(last[2])
  return total > 0 && done <= total ? { done, total } : undefined
}

/** Lines worth showing from a log's tail: no blanks, no carriage-return redraws. */
export function tailLines(text: string, count: number): string[] {
  return text
    .split('\n')
    .map(line => line.split('\r').pop()!.replace(/\x1b\[[0-9;]*[A-Za-z]/g, '').trimEnd())
    .filter(line => line.length > 0)
    .slice(-count)
}

export const formatDuration = (ms: number) => {
  const s = Math.max(0, Math.round(ms / 1000))
  if (s < 60) return `${s}s`
  const m = Math.floor(s / 60)
  if (m < 60) return `${m}m${String(s % 60).padStart(2, '0')}s`
  return `${Math.floor(m / 60)}h${String(m % 60).padStart(2, '0')}m`
}

/** The ninja edge on the last `[n/m]` line: `CXX obj/zitter/…/x.o`. */
export function ninjaStep(text: string): string | undefined {
  let step: string | undefined
  for (const m of text.matchAll(/\[\d+\/\d+\]\s+([^\r\n]+)/g)) step = m[1]
  return step?.replace(/\x1b\[[0-9;]*[A-Za-z]/g, '').trim()
}

export const ERROR_PATTERN = String.raw`FAILED:|error:|error\[|Error:|\[  FAILED  \]|Traceback`

/** Error lines from `grep -n` output: line numbers dropped, repeats folded, the last `max` kept. */
export function errorLines(grepOutput: string, max = 12): string[] {
  const seen = new Set<string>()
  const lines: string[] = []
  for (const raw of grepOutput.split('\n')) {
    const line = raw.replace(/^\d+:/, '').replace(/\x1b\[[0-9;]*[A-Za-z]/g, '').trim()
    if (!line || seen.has(line)) continue
    seen.add(line)
    lines.push(line)
  }
  return lines.slice(-max)
}
