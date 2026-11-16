// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
import { describe, expect, test } from 'claude-code/testing'

import { checkCommand, isBuildCommand } from '../hooks/guards'
import type { GuardContext } from '../hooks/guards'
import { body, errorLines, ninjaProgress, ninjaStep, script, tailLines, windowsBody } from '../hooks/jobs'
import { durationBars, median, remaining, ring, stopwatch } from '../hooks/draw'

const free: GuardContext = { approvals: [], outDirs: ['ZitterOptimized', 'ZitterContentBundle'] }
const refused = (command: string, ctx = free) => checkCommand(command, ctx) !== undefined

describe('guards', () => {
  test('ordinary commands pass', async () => {
    for (const command of [
      'git status',
      'ls .chromium/src/out/ZitterOptimized/zitter_prototype',
      'cat build.sh',
      'grep -rn ninja docs',
      '.chromium/src/third_party/ninja/ninja -C .chromium/src/out/ZitterOptimized sidebar_scene_unittests',
      'cd .chromium/src && third_party/ninja/ninja -C out/ZitterOptimized zitter_prototype',
      '.chromium/src/out/ZitterOptimized/zitter_prototype --gtest_filter=*SpacePanel*',
      '.chromium/src/out/ZitterOptimized/zitter_prototype --gtest_list_tests',
      'python3 -m unittest discover -s tests -p "test_*.py"',
      './build.sh --content --plan',
      'ZITTER_DEV_DIRECT=1 ./build.sh',
      'rm -rf build/rust-metal-profile',
      'cat .chromium/src/zitter_ui_lab/BUILD.gn',
    ]) {
      expect([command, checkCommand(command, free)]).toEqual([command, undefined])
    }
  })

  test('the frozen ui lab is never staged into', async () => {
    expect(refused('rsync -a zitter/ .chromium/src/zitter_ui_lab/')).toBe(true)
    expect(refused('cp foo.rs .chromium/src/zitter_ui_lab/src/')).toBe(true)
  })

  test('a bare ninja is Homebrew\'s', async () => {
    expect(refused('ninja -C .chromium/src/out/ZitterOptimized zitter')).toBe(true)
    expect(refused('cd .chromium/src && ninja -C out/ZitterOptimized zitter')).toBe(true)
    expect(refused('/opt/homebrew/bin/ninja -C out/ZitterOptimized')).toBe(true)
  })

  test('the full UI suite needs an approval', async () => {
    const bin = '.chromium/src/out/ZitterOptimized/zitter_prototype'
    expect(refused(bin)).toBe(true)
    expect(refused(`${bin} --gtest_filter='*'`)).toBe(true)
    expect(refused(`${bin} --gtest_filter=-Flaky*`)).toBe(true)
    expect(refused(`cd x && ./zitter_prototype_ui_lab`)).toBe(true)
    expect(refused(bin, { ...free, approvals: ['full-ui'] })).toBe(false)
  })

  test('a new out dir needs an approval', async () => {
    expect(refused('cd .chromium/src && buildtools/mac/gn gen out/ZitterDebug')).toBe(true)
    expect(refused('mkdir -p .chromium/src/out/Foo')).toBe(true)
    expect(refused('gn gen out/ZitterOptimized')).toBe(false)
    expect(refused('gn gen out/ZitterDebug', { ...free, approvals: ['outdir:ZitterDebug'] })).toBe(false)
  })

  test('deleting output dirs or the checkout', async () => {
    expect(refused('rm -rf .chromium/src/out/ZitterOptimized')).toBe(true)
    expect(refused('rm -rf .chromium')).toBe(true)
    expect(refused('rm -r -f out/ZitterContentDev/')).toBe(true)
    expect(refused('rm -rf .chromium/src/out/ZitterContentDev', { ...free, approvals: ['delete-out:ZitterContentDev'] })).toBe(false)
    expect(refused('robocopy empty C:\\x /MIR')).toBe(true)
  })

  test('a running build holds the build dir', async () => {
    const busy = { ...free, busy: 'pid 1: ./build.sh' }
    expect(refused('.chromium/src/third_party/ninja/ninja -C .chromium/src/out/ZitterOptimized zitter', busy)).toBe(true)
    expect(refused('git status', busy)).toBe(false)
  })

  test('builds go through zitter-dev', async () => {
    expect(refused('./build.sh')).toBe(true)
    expect(refused('./build.sh --run')).toBe(true)
    expect(refused('zsh .claude/skills/verify-ui/scripts/build_app.sh --run')).toBe(true)
    expect(refused('caffeinate -i ./build.sh')).toBe(true)
    expect(isBuildCommand('./build.sh')).toBe(true)
    expect(isBuildCommand('git log')).toBe(false)
  })
})

describe('guards on Windows', () => {
  const win: GuardContext = { approvals: [], outDirs: ['ZitterWin', 'ZitterWinDev'], platform: 'windows', outDir: 'out/ZitterWin' }

  test('backslash paths and Windows deletes', async () => {
    expect(refused('cmd /c rmdir /s /q .chromium\\src\\out\\ZitterWin', win)).toBe(true)
    expect(refused('Remove-Item -Recurse -Force .chromium\\src\\out\\ZitterWinDev', win)).toBe(true)
    expect(refused('cmd /c rmdir /s /q build\\old-worktree', win)).toBe(false)
    expect(refused('gn gen out\\ZitterWinNew', win)).toBe(true)
    expect(refused('powershell -File .claude\\skills\\verify-ui\\scripts\\build_app_win.ps1', win)).toBe(true)
  })

  test('a bare ninja is no Homebrew one on Windows', async () => {
    expect(refused('ninja -C .chromium/src/out/ZitterWin zitter', win)).toBe(false)
  })
})

describe('prose about ninja', () => {
  test('backticks in a heredoc are not commands', async () => {
    expect(refused("cat > f <<'EOF'\n/** The last `[done/total]` ninja wrote */\nEOF")).toBe(false)
    expect(refused("gh pr create --body-file - <<'EOF'\nrefuse staging into zitter_ui_lab > nothing\nEOF")).toBe(false)
    expect(refused("cat <<EOF > x\nhi\nEOF\nrsync -a zitter/ .chromium/src/zitter_ui_lab/")).toBe(true)
    expect(refused('git commit -m "Build it\n(./build.sh --content on macOS)"')).toBe(false)
    expect(refused("rm -rf '.chromium/src/out/ZitterOptimized'")).toBe(true)
  })
})

describe('jobs', () => {
  test('ninja progress is the last counter', async () => {
    expect(ninjaProgress('[1/10] CXX a\n[7/10] CXX b\n')).toEqual({ done: 7, total: 10 })
    expect(ninjaProgress('no counter')).toBeUndefined()
  })

  test('tail lines drop blanks and redraws', async () => {
    expect(tailLines('a\n\nb\rc\n\x1b[1mbold\x1b[0m\n', 2)).toEqual(['c', 'bold'])
  })

  test('every job builds or runs the Content browser', async () => {
    expect(body('build')).toBe('./build.sh --content')
    expect(body('run')).toContain('./build.sh --content && ')
    expect(body('run')).toContain('$LAUNCH')
    expect(windowsBody('build')).toContain('autoninja -C .chromium\\src\\out\\ZitterWinContent zitter_content')
    expect(windowsBody('build')).toContain('stage_content_embedder.py')
    expect(script('macos', '/r', '/x.exit', 'Build', 'run', '/h/zjob.py', ['python3'])).toContain("LAUNCH=('python3' '/h/zjob.py' launch '/r')")
  })
})

describe('panel numbers', () => {
  const job = (over: Record<string, unknown>) =>
    ({ id: 'j', kind: 'run', title: 'Build & run', state: 'ok', by: 'person', queuedAt: 0, startedAt: 0, endedAt: 600_000, ...over }) as never

  test('ninja step and error lines', async () => {
    expect(ninjaStep('[1/9] CXX a.o\n[2/9] LINK zitter\nnoise')).toBe('LINK zitter')
    expect(errorLines('12:a.cc:1: error: x\n40:a.cc:1: error: x\n41:FAILED: a.o')).toEqual(['a.cc:1: error: x', 'FAILED: a.o'])
  })

  test('ETA from the pace, else from past runs', async () => {
    const running = { ...(job({ state: 'running', startedAt: 0 }) as object), progress: { done: 50, total: 100 } } as never
    expect(remaining(running, [], 300_000)).toBe(300_000)
    const early = job({ state: 'running', startedAt: 0 })
    expect(remaining(early, [job({}), job({ endedAt: 800_000 })], 100_000)).toBe(600_000)
    expect(remaining(early, [], 100_000)).toBeUndefined()
    expect(median([3, 1, 2])).toBe(2)
  })

  test('svg pieces', async () => {
    expect(ring(0.3)).toContain('stroke-dasharray')
    expect(durationBars([job({}), job({ state: 'failed' })], 100)).toContain('#D2493F')
    expect(stopwatch(252_000)).toBe('4:12')
  })
})
