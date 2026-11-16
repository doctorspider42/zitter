// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// What each zitter-dev job runs. A job is one script (zsh on macOS, PowerShell
// on Windows) that helper/zjob.py starts detached in its own process group, so
// it outlives a reload of the mod and can be stopped as a whole. Every job
// builds or runs the Content browser (`//zitter_content:zitter_content`).

import type { JobKind } from '../types'

export type Platform = 'macos' | 'windows'

/** The Content output directory each platform builds in. */
export const OUT_DIR: Record<Platform, string> = { macos: 'out/ZitterContentBundle', windows: 'out/ZitterWinContent' }

export type KindInfo = {
  title: string
  /** One line for the command menu and the model's tool description. */
  description: string
  /** Whether it stages or builds in the shared build directory. */
  usesBuildDir: boolean
}

export const KINDS: Record<JobKind, KindInfo> = {
  build: { title: 'Build', description: 'stage and build the Content browser (macOS: ./build.sh --content → build/ZitterContentLab.app; Windows: out/ZitterWinContent)', usesBuildDir: true },
  run: { title: 'Build & run', description: 'build the Content browser, then relaunch it', usesBuildDir: true },
  launch: { title: 'Launch', description: 'relaunch the last Content build without building', usesBuildDir: false },
  test: { title: 'Tests', description: 'run the Python tests (tests/test_*.py)', usesBuildDir: false },
}

export const KIND_NAMES = Object.keys(KINDS) as JobKind[]

export const isKind = (kind: string): kind is JobKind => kind in KINDS

/** A single-quoted shell word. */
export const sq = (s: string) => `'${s.replace(/'/g, `'\\''`)}'`

/** The body of a macOS job's script, run from the repository root. */
export function body(kind: JobKind): string {
  switch (kind) {
    case 'build':
      return './build.sh --content'
    case 'run':
      return './build.sh --content && echo "== Relaunching the Content browser" && $LAUNCH'
    case 'launch':
      return '$LAUNCH'
    case 'test':
      return "python3 -m unittest discover -s tests -p 'test_*.py'"
  }
}

/** The whole script of a job: it writes `<exit code> <end time>` when it ends. */
export function script(platform: Platform, root: string, exitFile: string, title: string, kind: JobKind, helper: string, python: readonly string[]): string {
  if (platform === 'windows') return windowsScript(root, exitFile, title, kind, helper, python)
  return [
    '#!/bin/zsh',
    `cd -- ${sq(root)} || { print -r -- "1 $(date +%s)" > ${sq(exitFile)}; exit 1 }`,
    'set -o pipefail',
    `LAUNCH=(${python.map(sq).join(' ')} ${sq(helper)} launch ${sq(root)})`,
    `print -r -- "== zitter-dev: ${title.replace(/["$`\\]/g, '')} ($(date '+%H:%M:%S'), $(git rev-parse --short HEAD))"`,
    '(',
    body(kind),
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

/** The body of a Windows job, in PowerShell, run from the repository root (docs/remote-builds.md). */
export function windowsBody(kind: JobKind): string {
  const stage = 'python tools\\stage_content_embedder.py .chromium\\src; if ($LASTEXITCODE) { exit $LASTEXITCODE }'
  const ready =
    "if (-not (Test-Path .chromium\\src\\out\\ZitterWinContent\\args.gn)) { Write-Output 'Set up out/ZitterWinContent first (docs/remote-builds.md).'; exit 2 }"
  const build = 'autoninja -C .chromium\\src\\out\\ZitterWinContent zitter_content; if ($LASTEXITCODE) { exit $LASTEXITCODE }'
  const launch = '& $Launch; exit $LASTEXITCODE'
  switch (kind) {
    case 'build':
      return [ready, stage, build].join('\n')
    case 'run':
      return [ready, stage, build, "Write-Output '== Relaunching the Content browser'", launch].join('\n')
    case 'launch':
      return launch
    case 'test':
      return "python -m unittest discover -s tests -p 'test_*.py'; exit $LASTEXITCODE"
  }
}

function windowsScript(root: string, exitFile: string, title: string, kind: JobKind, helper: string, python: readonly string[]): string {
  // The body runs as a script block, so its `exit` ends the block and the exit file is still written.
  const [exe = 'python', ...pre] = python
  return [
    "$ErrorActionPreference = 'Continue'",
    "$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'",
    `Set-Location -LiteralPath ${psq(root)}`,
    "$env:PATH = \"$(Join-Path (Get-Location) '.chromium\\depot_tools');$env:PATH\"",
    `$Launch = { & ${psq(exe)} ${[...pre, helper, 'launch', root].map(psq).join(' ')} }`,
    `Write-Output "== zitter-dev: ${title.replace(/["$`]/g, '')} ($(Get-Date -Format HH:mm:ss), $(git rev-parse --short HEAD))"`,
    '$job = {',
    windowsBody(kind),
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
