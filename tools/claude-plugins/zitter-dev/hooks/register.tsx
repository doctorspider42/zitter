// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
import { atom, read, update } from 'claude-code'
import type { CodeProps, ElementConstructor, MarkdownProps, EngineInterface, On, ProgressProps, Register, SvgProps } from 'claude-code'

import type { Approval, Blocked, Job, JobKind, Lock, Repo } from '../types'
import { checkCommand, isBuildCommand } from './guards'
import { ERROR_PATTERN, KIND_NAMES, KINDS, OUT_DIR, errorLines, formatDuration, isKind, ninjaProgress, ninjaStep, script, tailLines } from './jobs'
import type { Platform } from './jobs'
import { COLORS, badge, dot, durationBars, grouped, icon, median, duration, minutesLeft, remaining, ring, stopwatch } from './draw'

type $ = EngineInterface

const PANE = 'zitter'
const APPROVAL_MS = 30 * 60 * 1000
const KEEP_JOBS = 30

const jobsAtom = atom({ plugin: 'zitter-dev', key: 'jobs' } as const, [] as Job[], { shape: 'jobs-2' })
const lockAtom = atom({ plugin: 'zitter-dev', key: 'lock' } as const, { checkedAt: 0 } as Lock)
const repoAtom = atom({ plugin: 'zitter-dev', key: 'repo' } as const, { root: '', branch: '', head: '', dirty: 0 } as Repo)
const approvalsAtom = atom({ plugin: 'zitter-dev', key: 'approvals' } as const, [] as Approval[])
const blockedAtom = atom({ plugin: 'zitter-dev', key: 'blocked' } as const, [] as Blocked[])
const shownLogAtom = atom({ plugin: 'zitter-dev', key: 'shownLog' } as const, '')
const autoOpenedAtom = atom({ plugin: 'zitter-dev', key: 'autoOpened' } as const, false)
const dismissedAtom = atom({ plugin: 'zitter-dev', key: 'dismissed' } as const, '')

// The module's own variables start over on a reload; session.start sets them again.
let root: string | undefined
let jobsDir = ''
let platform: Platform = 'macos'
/** The Python that runs helper/zjob.py: an argv prefix. */
let python: string[] = ['python3']
let helperPath = ''
let outDirs: string[] = []
let ticking = false
let lastEnvAt = 0

const isDone = (job: Job) => job.state === 'ok' || job.state === 'failed' || job.state === 'stopped'
const filesOf = (id: string) => ({
  script: `${jobsDir}/${id}.${platform === 'windows' ? 'ps1' : 'zsh'}`,
  log: `${jobsDir}/${id}.log`,
  exit: `${jobsDir}/${id}.exit`,
  meta: `${jobsDir}/${id}.json`,
})
const clock = (ms: number) => new Date(ms).toTimeString().slice(0, 5)

async function sh($: $, argv: string[], timeoutMs = 15_000) {
  try {
    return await $.process.run(argv, { cwd: root, timeoutMs })
  } catch {
    return { exitCode: 127, stdout: '', stderr: '', isStdoutTruncated: false, isStderrTruncated: false }
  }
}

/** Runs a subcommand of helper/zjob.py. */
const helper = ($: $, args: string[], timeoutMs?: number) => sh($, [...python, helperPath, ...args], timeoutMs)

type HostStatus = { busy: string | null; appRunning: boolean; appBuiltAt: number | null; diskFree: number }

async function hostStatus($: $): Promise<HostStatus | undefined> {
  if (!root) return
  try {
    return JSON.parse((await helper($, ['status', root])).stdout) as HostStatus
  } catch {
    return undefined
  }
}

async function writeMeta($: $, job: Job) {
  const { tail, progress, ...meta } = job
  await $.fs.write(filesOf(job.id).meta, JSON.stringify(meta) + '\n')
}

async function setJob($: $, id: string, change: (job: Job) => Job): Promise<Job | undefined> {
  let changed: Job | undefined
  await update($, jobsAtom, jobs =>
    jobs.map(job => (job.id === id ? (changed = change(job)) : job)),
  )
  if (changed) await writeMeta($, changed)
  return changed
}

// ---------------------------------------------------------------------------
// The shared build directory

async function externalBuild($: $): Promise<string | undefined> {
  return (await hostStatus($))?.busy ?? undefined
}

async function whatHoldsBuildDir($: $, jobs: readonly Job[]): Promise<string | undefined> {
  const mine = jobs.find(job => job.state === 'running' && KINDS[job.kind].usesBuildDir)
  if (mine) return `this session's ${mine.title} (${mine.id})`
  return externalBuild($)
}

// ---------------------------------------------------------------------------
// Jobs

async function enqueue($: $, kind: JobKind, by: Job['by'], filter?: string): Promise<Job> {
  const now = Date.now()
  const job: Job = {
    id: `j${now.toString(36)}${Math.floor(Math.random() * 36 ** 2).toString(36)}`,
    kind,
    title: KINDS[kind].title,
    state: 'queued',
    by,
    filter,
    queuedAt: now,
    log: '',
  }
  job.log = filesOf(job.id).log
  await update($, jobsAtom, jobs => [...jobs, job])
  await writeMeta($, job)
  void tick($)
  return job
}

async function start($: $, job: Job): Promise<boolean> {
  if (!root) return false
  if (KINDS[job.kind].usesBuildDir) {
    const busy = await externalBuild($)
    if (busy) {
      await update($, lockAtom, () => ({ busy, checkedAt: Date.now() }))
      return false
    }
  }
  const files = filesOf(job.id)
  await $.fs.write(files.script, script(platform, root, files.exit, job.title, job.kind, helperPath, python))
  const head = (await sh($, ['git', 'rev-parse', '--short', 'HEAD'])).stdout.trim()
  const launched = await helper($, ['start', files.script, files.log])
  const pid = Number(launched.stdout.trim())
  if (launched.exitCode !== 0 || !Number.isInteger(pid) || pid <= 0) {
    await setJob($, job.id, j => ({ ...j, state: 'failed', endedAt: Date.now(), tail: ['Could not start the job.', launched.stderr.trim()] }))
    $.ui.toast(`zitter-dev: could not start ${job.title}`)
    return false
  }
  await setJob($, job.id, j => ({ ...j, state: 'running', startedAt: Date.now(), pid, head }))
  return true
}

async function stop($: $, job: Job) {
  if (job.state === 'queued') {
    await setJob($, job.id, j => ({ ...j, state: 'stopped', endedAt: Date.now() }))
    return
  }
  if (job.state === 'running' && job.pid) {
    // The job leads its own process group (a tree on Windows): ninja and the app launcher go too.
    await helper($, ['kill', String(job.pid)])
  }
}

async function finish($: $, job: Job, code: number | undefined, endedAt: number) {
  const state: Job['state'] = code === undefined ? 'stopped' : code === 0 ? 'ok' : 'failed'
  const errors =
    state === 'failed'
      ? errorLines((await helper($, ['grep', filesOf(job.id).log, ERROR_PATTERN])).stdout)
      : undefined
  const done = await setJob($, job.id, j => ({ ...j, state, exitCode: code, endedAt, errors }))
  if (!done) return
  const took = formatDuration(endedAt - (done.startedAt ?? done.queuedAt))
  if (state === 'ok' && (done.kind === 'build' || done.kind === 'run') && done.head) {
    await $.store.set('builtHead', done.head)
    lastEnvAt = 0
  }
  const text =
    state === 'ok' ? `✓ ${done.title} done in ${took}`
    : state === 'failed' ? `✗ ${done.title} failed (exit ${code}) after ${took}${errors?.length ? `: ${errors[errors.length - 1]?.slice(0, 80)}` : ''}`
    : `■ ${done.title} stopped after ${took}`
  $.ui.toast(text, { timeoutMs: 8000 })
  if (endedAt - (done.startedAt ?? endedAt) > 30_000) void $.ui.notify(text, { title: 'Zitter' })
}

async function readExit($: $, id: string): Promise<{ code: number; at: number } | undefined> {
  const file = filesOf(id).exit
  if (!(await $.fs.exists(file))) return
  const [code = NaN, at = NaN] = (await $.fs.read(file)).trim().split(/\s+/).map(Number)
  return { code: Number.isFinite(code) ? code : 1, at: Number.isFinite(at) ? at * 1000 : Date.now() }
}

async function isAlive($: $, pid: number) {
  return (await helper($, ['alive', String(pid)])).exitCode === 0
}

async function tick($: $) {
  if (ticking || !root) return
  ticking = true
  try {
    const jobs = await read($, jobsAtom)
    for (const job of jobs.filter(j => j.state === 'running')) {
      const exit = await readExit($, job.id)
      const raw = (await helper($, ['tail', job.log ?? filesOf(job.id).log, '8000'])).stdout
      const progress = ninjaProgress(raw)
      const tail = tailLines(raw, 12)
      const step = ninjaStep(raw) ?? job.step
      if (exit) {
        await update($, jobsAtom, js => js.map(j => (j.id === job.id ? { ...j, progress, tail, step } : j)))
        await finish($, job, exit.code, exit.at)
      } else if (job.pid && !(await isAlive($, job.pid))) {
        const late = await readExit($, job.id)
        await update($, jobsAtom, js => js.map(j => (j.id === job.id ? { ...j, progress, tail, step } : j)))
        await finish($, job, late?.code, late?.at ?? Date.now())
      } else if (JSON.stringify(tail) !== JSON.stringify(job.tail) || progress?.done !== job.progress?.done) {
        await update($, jobsAtom, js => js.map(j => (j.id === job.id ? { ...j, progress, tail, step } : j)))
      }
    }

    const now = await read($, jobsAtom)
    const running = now.find(j => j.state === 'running')
    const next = now.find(j => j.state === 'queued')
    if (!running && next) await start($, next)

    if (Date.now() - lastEnvAt > 6000) {
      lastEnvAt = Date.now()
      await refreshEnvironment($)
    }
    await showStatus($)
  } finally {
    ticking = false
  }
}

async function showStatus($: $) {
  const jobs = await read($, jobsAtom)
  const running = jobs.find(j => j.state === 'running')
  const queued = jobs.filter(j => j.state === 'queued')
  if (running) {
    const p = running.progress
    const took = formatDuration(Date.now() - (running.startedAt ?? Date.now()))
    const step = p ? ` ${p.done}/${p.total} (${Math.floor((100 * p.done) / p.total)}%)` : ''
    $.ui.status(`⚙ ${running.title}${step} · ${took}${queued.length ? ` · +${queued.length} queued` : ''}`)
  } else if (queued.length) {
    const lock = await read($, lockAtom)
    $.ui.status(`⏳ ${queued[0]?.title} waits for the build dir${lock.busy ? `: ${lock.busy}` : ''}`)
  } else {
    $.ui.status(undefined)
  }
}

async function refreshEnvironment($: $) {
  const repoRoot = root
  if (!repoRoot) return
  const jobs = await read($, jobsAtom)
  const host = await hostStatus($)
  const mine = jobs.find(job => job.state === 'running' && KINDS[job.kind].usesBuildDir)
  const busy = mine ? `this session's ${mine.title} (${mine.id})` : host?.busy ?? undefined
  await update($, lockAtom, () => ({ busy, checkedAt: Date.now() }))

  const branch = (await sh($, ['git', 'rev-parse', '--abbrev-ref', 'HEAD'])).stdout.trim()
  const head = (await sh($, ['git', 'rev-parse', '--short', 'HEAD'])).stdout.trim()
  const status = (await sh($, ['git', 'status', '--porcelain'])).stdout
  const builtHead = ((await $.store.get('builtHead')) as string | undefined) ?? undefined
  let behind: number | undefined
  if (builtHead) {
    const count = await sh($, ['git', 'rev-list', '--count', `${builtHead}..HEAD`])
    if (count.exitCode === 0) behind = Number(count.stdout.trim())
  }
  const appBuiltAt = host?.appBuiltAt ?? undefined
  if (!builtHead && appBuiltAt) {
    const since = await sh($, ['git', 'rev-list', '--count', `--since=${Math.floor(appBuiltAt / 1000)}`, 'HEAD'])
    if (since.exitCode === 0) behind = Number(since.stdout.trim())
  }
  const appRunning = host?.appRunning ?? false
  await update($, repoAtom, () => ({
    root: repoRoot,
    branch,
    head,
    dirty: status.split('\n').filter(Boolean).length,
    builtHead,
    behind,
    diskFree: host ? `${Math.round(host.diskFree / 1e9)} GB` : undefined,
    appBuiltAt,
    appRunning,
  }))

  try {
    outDirs = (await $.fs.list(`${root}/.chromium/src/out`)).filter(e => e.kind === 'dir').map(e => e.name)
  } catch {
    outDirs = []
  }
}

/** Loads the last jobs of earlier sessions from disk, and prunes old files. */
async function loadHistory($: $) {
  const entries = await $.fs.list(jobsDir).catch(() => [])
  const ids = [...new Set(entries.map(e => e.name.replace(/\.[^.]+$/, '')).filter(id => /^j[0-9a-z]+$/.test(id)))].sort()
  const old = ids.slice(0, Math.max(0, ids.length - KEEP_JOBS))
  if (old.length) {
    await helper($, ['rm', ...old.flatMap(id => [...Object.values(filesOf(id)), `${jobsDir}/${id}.zsh`, `${jobsDir}/${id}.ps1`])])
  }
  // Finished jobs whose files were cleaned away leave the list too.
  const present = new Set(ids)
  await update($, jobsAtom, jobs => jobs.filter(j => !isDone(j) || present.has(j.id)))
  const known = new Set((await read($, jobsAtom)).map(j => j.id))
  const loaded: Job[] = []
  for (const id of ids.slice(-8)) {
    if (known.has(id) || !(await $.fs.exists(filesOf(id).meta))) continue
    try {
      const job = JSON.parse(await $.fs.read(filesOf(id).meta)) as Job
      // A job another session runs stays that session's to track; Chrome-era kinds are gone.
      if (!isDone(job) || !isKind(job.kind)) continue
      loaded.push(job)
    } catch {
      // A meta file cut short: skip it.
    }
  }
  if (loaded.length) await update($, jobsAtom, jobs => [...loaded, ...jobs].sort((a, b) => a.queuedAt - b.queuedAt))
}

async function activeApprovals($: $): Promise<string[]> {
  const now = Date.now()
  return (await read($, approvalsAtom)).filter(a => a.until > now).map(a => a.what)
}

// ---------------------------------------------------------------------------
// Actions the panel offers

const retry = ($: $, job: Job) => enqueue($, job.kind, 'person', job.filter)

async function askToFix($: $, job: Job) {
  const errors = job.errors?.length ? job.errors.join('\n') : (job.tail ?? []).join('\n')
  await $.prompt.submit({
    text:
      `The Zitter job "${job.title}" failed (exit ${job.exitCode ?? '?'}). Error lines:\n\n\`\`\`\n${errors}\n\`\`\`\n\n` +
      `Full log: ${filesOf(job.id).log}\nFind the cause, fix it, then run the job again with zitter_dev_start (kind: ${job.kind}${job.filter ? `, filter: ${job.filter}` : ''}) and wait for it.`,
  })
}

const openLog = ($: $, job: Job) => helper($, ['open', filesOf(job.id).log])

async function quitApp($: $) {
  if (!root) return
  await helper($, ['quit-app', root])
  lastEnvAt = 0
}

/** Hides a failed job's card and band; the job stays in Recent. */
const dismiss = ($: $, job: Job) => update($, dismissedAtom, () => job.id)

const toggleShown = ($: $, what: string) => update($, shownLogAtom, shown => (shown === what ? '' : what))

// ---------------------------------------------------------------------------
// The model's tools: start a job, wait for one.

async function waitFor($: $, id: string, timeoutMs: number, signal?: AbortSignal): Promise<string> {
  const deadline = Date.now() + timeoutMs
  const files = filesOf(id)
  for (;;) {
    const meta = (await $.fs.exists(files.meta)) ? (JSON.parse(await $.fs.read(files.meta)) as Job) : undefined
    if (!meta) return `No job ${id}.`
    const exit = await readExit($, id)
    const dead = meta.state === 'running' && meta.pid !== undefined && !(await isAlive($, meta.pid))
    if (exit || dead || isDone(meta) || Date.now() >= deadline || signal?.aborted) {
      return report($, meta, exit?.code, exit ? 'ended' : dead || isDone(meta) ? 'ended' : 'still going')
    }
    await helper($, ['sleep', '3'])
  }
}

async function report($: $, job: Job, code: number | undefined, phase: string): Promise<string> {
  const files = filesOf(job.id)
  const hasLog = await $.fs.exists(files.log)
  const tail = hasLog ? (await helper($, ['tail', files.log, '12000'])).stdout : ''
  const errors =
    hasLog && code !== undefined && code !== 0
      ? (await helper($, ['grep', files.log, ERROR_PATTERN])).stdout
          .trim().split('\n').slice(-40).join('\n')
      : ''
  const state = code === undefined ? (phase === 'ended' ? 'stopped' : job.state) : code === 0 ? 'ok' : `failed (exit ${code})`
  const lock = await read($, lockAtom)
  return [
    `Job ${job.id}: ${job.title}: ${state}${phase === 'still going' ? ' (still going; call zitter_dev_wait again)' : ''}.`,
    job.state === 'queued' && lock.busy ? `Waiting for the shared build dir: ${lock.busy}.` : '',
    `Log: ${files.log}`,
    errors ? `Error lines:\n${errors}` : '',
    tail ? `Last lines:\n${tailLines(tail, 60).join('\n')}` : '',
  ].filter(Boolean).join('\n')
}

// ---------------------------------------------------------------------------

const COMMANDS: { name: string; kind: JobKind; argumentHint?: string }[] = [
  { name: 'dev', kind: 'build' },
  { name: 'dev-run', kind: 'run' },
  { name: 'dev-launch', kind: 'launch' },
  { name: 'dev-test', kind: 'test' },
]

export const register: Register = on => {
  on('session.start', async ($, e, next) => {
    const started = await next(e)
    const top = await $.process.run(['git', 'rev-parse', '--show-toplevel'], { cwd: e.cwd }).catch(() => undefined)
    const candidate = top?.exitCode === 0 ? top.stdout.trim() : undefined
    if (!candidate || !(await $.fs.exists(`${candidate}/build.sh`)) || !(await $.fs.exists(`${candidate}/chromium`))) {
      return started // Not a Zitter checkout: the mod stays quiet.
    }
    helperPath = `${$.plugin.root}/helper/zjob.py`
    let info: { platform: Platform; jobsDir: string } | undefined
    for (const argv of [['python3'], ['python'], ['py', '-3'], ['/usr/bin/python3']]) {
      const probe = await $.process.run([...argv, helperPath, 'info'], { cwd: e.cwd }).catch(() => undefined)
      if (probe?.exitCode !== 0) continue
      try {
        info = JSON.parse(probe.stdout)
        python = argv
        break
      } catch {
        // A stub that prints something else (the Windows Store alias): try the next.
      }
    }
    if (!info) {
      $.ui.toast('zitter-dev: no Python 3 on PATH; the mod stays off.')
      return started
    }
    root = candidate
    platform = info.platform
    jobsDir = info.jobsDir

    for (const c of COMMANDS) {
      await $.command.register({ name: c.name, description: `Zitter: ${KINDS[c.kind].description}`, argumentHint: c.argumentHint })
    }
    await $.command.register({ name: 'dev-stop', description: 'Zitter: stop the running job and clear the queue' })
    await $.command.register({ name: 'dev-log', description: 'Zitter: show the end of the last job\'s log', argumentHint: '[lines]' })
    await $.command.register({ name: 'dev-allow', description: 'Zitter: allow for 30 min what the guards refuse', argumentHint: 'outdir <name> | delete-out <name>' })
    await $.command.register({ name: 'zitter', description: 'Zitter: open the dev panel' })

    await $.tool.register({
      name: 'zitter_dev_start',
      description:
        `Start a Zitter dev job detached, in the queue of the shared build directory (.chromium/src/${OUT_DIR[platform]}), and return its id at once. ` +
        'Use this instead of running ./build.sh or build_app.sh in Bash. The person sees its progress. Kinds: ' +
        KIND_NAMES.map(k => `${k}: ${KINDS[k].description}`).join('; ') +
        '. Then call zitter_dev_wait with the id.',
      inputSchema: {
        type: 'object',
        properties: {
          kind: { type: 'string', enum: KIND_NAMES },
        },
        required: ['kind'],
      },
      isDeferred: false,
    })
    await $.tool.register({
      name: 'zitter_dev_wait',
      description:
        'Wait for a Zitter dev job to end (up to timeout_seconds, default 540) and return its result, error lines and the end of its log. ' +
        'timeout_seconds 0 only reports where it stands. Without an id, the latest job.',
      inputSchema: {
        type: 'object',
        properties: { id: { type: 'string' }, timeout_seconds: { type: 'number', minimum: 0, maximum: 570 } },
      },
      isDeferred: false,
    })

    await loadHistory($)
    await refreshEnvironment($)
    lastEnvAt = Date.now()
    $.clock.every(2000, () => void tick($))
    void tick($)

    // A surface may already be attached (a reload): open the panel now; otherwise session.attach does.
    if (!(await read($, autoOpenedAtom)) && (await $.session.surfaces()).length > 0) {
      const opened = await $.ui.open({ id: PANE, title: 'Zitter' }).catch(() => undefined)
      if (opened?.isPlaced) await update($, autoOpenedAtom, () => true)
    }
    return started
  })

  // Open the panel by itself the first time a surface attaches in a Zitter checkout.
  on('session.attach', async ($, e, next) => {
    const attached = await next(e)
    if (root && !(await read($, autoOpenedAtom))) {
      const opened = await $.ui.open({ id: PANE, title: 'Zitter' }).catch(() => undefined)
      if (opened?.isPlaced) await update($, autoOpenedAtom, () => true)
    }
    return attached
  })

  for (const c of COMMANDS) {
    on('command.run', { command: c.name }, async ($, e) => {
      if (!root) return { text: 'zitter-dev: this session is not in a Zitter checkout.' }
      const kind = c.kind
      const job = await enqueue($, kind, 'person')
      void $.ui.open({ id: PANE, title: 'Zitter', placement: 'widget' })
      const lock = await read($, lockAtom)
      const waits = KINDS[kind].usesBuildDir && lock.busy
      return { text: `${job.title} ${waits ? `queued behind ${lock.busy}` : 'started'} (${job.id}).` }
    })
  }

  on('command.run', { command: 'dev-stop' }, async $ => {
    const jobs = await read($, jobsAtom)
    const live = jobs.filter(j => j.state === 'running' || j.state === 'queued')
    for (const job of live) await stop($, job)
    void tick($)
    return { text: live.length ? `Stopping ${live.map(j => j.title).join(', ')}.` : 'Nothing is running.' }
  })

  on('command.run', { command: 'dev-log' }, async ($, e) => {
    const jobs = await read($, jobsAtom)
    const last = jobs[jobs.length - 1]
    if (!last) return { text: 'No jobs yet.' }
    const lines = Math.min(400, Math.max(5, Number(e.args.trim()) || 40))
    const out = (await helper($, ['tail', filesOf(last.id).log, String(lines * 300)])).stdout
    return { text: `${last.title} (${last.state}) — ${filesOf(last.id).log}\n\`\`\`\n${tailLines(out, lines).join('\n')}\n\`\`\`` }
  })

  on('command.run', { command: 'dev-allow' }, async ($, e) => {
    // Only the person grants these: never a plugin's (or a model-driven) run.
    if (e.origin.kind === 'plugin') return { text: 'zitter-dev: approvals come from the person only.' }
    const [what, name] = e.args.trim().split(/\s+/)
    const key = (what === 'outdir' || what === 'delete-out') && name && /^[\w.-]+$/.test(name) ? `${what}:${name}` : undefined
    if (!key) return { text: 'Usage: /dev-allow outdir <name> | delete-out <name>' }
    const until = Date.now() + APPROVAL_MS
    await update($, approvalsAtom, list => [...list.filter(a => a.what !== key && a.until > Date.now()), { what: key, until }])
    return { text: `Allowed ${key.replace(':', ' ')} until ${clock(until)}.` }
  })

  on('command.run', { command: 'zitter' }, async $ => {
    await $.ui.open({ id: PANE, title: 'Zitter', focus: true })
    return { text: 'Zitter panel opened.' }
  })

  // -------------------------------------------------------------------------
  // Guards

  on('tool.call', { tool: 'Bash' }, async ($, e, next) => {
    if (!root) return next(e)
    const command = e.command
    const jobs = await read($, jobsAtom)
    const busy = isBuildCommand(command) ? await whatHoldsBuildDir($, jobs) : undefined
    const reason = checkCommand(command, { approvals: await activeApprovals($), outDirs, busy, platform, outDir: OUT_DIR[platform] })
    if (!reason) return next(e)
    await update($, blockedAtom, list => [...list, { at: Date.now(), command: command.slice(0, 200), reason }].slice(-20))
    $.ui.toast(`zitter-dev blocked: ${(command.split('\n')[0] ?? '').slice(0, 60)}`)
    return { deny: `zitter-dev: ${reason}` }
  }).catch(($, e, next) => (next.called ? next(e) : next(e))) // a broken guard never blocks work

  on('tool.call', { tool: 'mcp__zitter-dev__zitter_dev_start' }, async ($, e) => {
    if (!root) return { deny: 'zitter-dev: this session is not in a Zitter checkout.' }
    const input = e as unknown as { kind?: string }
    const kind = KIND_NAMES.find(k => k === input.kind)
    if (!kind) return { deny: `Unknown kind ${input.kind}; one of ${KIND_NAMES.join(', ')}.` }
    const job = await enqueue($, kind, 'model')
    const lock = await read($, lockAtom)
    const waits = KINDS[kind].usesBuildDir && lock.busy
    return {
      result: `Job ${job.id} (${job.title}) ${waits ? `queued behind ${lock.busy}` : 'started'}. Log: ${job.log}. Call zitter_dev_wait {"id": "${job.id}"}.`,
    }
  })

  on('tool.call', { tool: 'mcp__zitter-dev__zitter_dev_wait' }, async ($, e, next) => {
    if (!root) return { deny: 'zitter-dev: this session is not in a Zitter checkout.' }
    const input = e as unknown as { id?: string; timeout_seconds?: number }
    const jobs = await read($, jobsAtom)
    const id = input.id ?? jobs[jobs.length - 1]?.id
    if (!id) return { result: 'No jobs yet.' }
    const seconds = Math.min(570, Math.max(0, input.timeout_seconds ?? 540))
    return { result: await waitFor($, id, seconds * 1000, next.signal) }
  })

  drawing(on)
}

// ---------------------------------------------------------------------------
// Drawing: the panel, its widget form and the band above the prompt.

type Model = {
  jobs: Job[]
  lock: Lock
  repo: Repo
  approvals: Approval[]
  blocked: Blocked[]
  shown: string
  /** The last job, when it failed and the person has not dismissed it. */
  failure?: Job
  running?: Job
  queued: Job[]
  recent: Job[]
  last?: Job
  now: number
}

async function model($: $): Promise<Model> {
  const jobs = await read($, jobsAtom)
  const lock = await read($, lockAtom)
  const finished = jobs.filter(isDone)
  const last = finished.at(-1)
  const dismissed = await read($, dismissedAtom)
  return {
    failure: last?.state === 'failed' && last.id !== dismissed ? last : undefined,
    jobs,
    lock,
    repo: await read($, repoAtom),
    approvals: (await read($, approvalsAtom)).filter(a => a.until > Date.now()),
    blocked: await read($, blockedAtom),
    shown: await read($, shownLogAtom),
    running: jobs.find(j => j.state === 'running'),
    queued: jobs.filter(j => j.state === 'queued'),
    recent: finished.slice(-5).reverse(),
    last: finished.at(-1),
    now: Math.max(Date.now(), lock.checkedAt),
  }
}

const BUILDS = new Set<JobKind>(['build', 'run'])

function freshness(repo: Repo): { text: string; color: string } {
  const changed = repo.dirty ? ` · ${repo.dirty} file${repo.dirty === 1 ? '' : 's'} changed` : ' · clean tree'
  if (!repo.builtHead) {
    // Built outside the mod: judge it by the package's age against the commits since.
    if (repo.appBuiltAt === undefined) return { text: `No app built yet${changed}`, color: COLORS.muted }
    if (repo.behind === 0) return { text: `App built ${ago(repo.appBuiltAt)}${changed}`, color: repo.dirty ? '#C99A2E' : COLORS.success }
    return { text: `App is ${repo.behind ?? '?'} commit${repo.behind === 1 ? '' : 's'} behind${changed}`, color: '#C99A2E' }
  }
  if (repo.behind === 0) return { text: `App is up to date${changed}`, color: repo.dirty ? '#C99A2E' : COLORS.success }
  if (repo.behind !== undefined) return { text: `App is ${repo.behind} commit${repo.behind === 1 ? '' : 's'} behind${changed}`, color: '#C99A2E' }
  return { text: `App built from ${repo.builtHead}${changed}`, color: COLORS.muted }
}

function ago(ms: number): string {
  const minutes = Math.round((Date.now() - ms) / 60_000)
  if (minutes < 60) return `${Math.max(1, minutes)} min ago`
  const hours = Math.round(minutes / 60)
  return hours < 24 ? `${hours} h ago` : `${Math.round(hours / 24)} d ago`
}

const resultLine = (job: Job) =>
  `${job.state === 'failed' ? `exit ${job.exitCode} · after ` : ''}${formatDuration(duration(job))} · ${clock(job.endedAt ?? job.queuedAt)}`

// One table per surface: the desktop has Svg, Progress, Code and Markdown; elsewhere text stands in.
// This engine's Text takes bold, colors and wrap, not size, weight or mono: headings and mono go through Markdown.
function kit($: $, e: Parameters<$['ui']['resolve']>[0]) {
  const els = $.ui.resolve(e)
  const { Box, Text, Button } = els
  const extra = els as unknown as {
    Markdown?: ElementConstructor<MarkdownProps>
    Svg?: ElementConstructor<SvgProps>
    Progress?: ElementConstructor<ProgressProps>
    Code?: ElementConstructor<CodeProps>
  }
  const Svg = extra.Svg
  const Progress = extra.Progress
  const Code = extra.Code
  const Markdown = extra.Markdown
  const md = (text: string, fallback: string) => (Markdown ? <Markdown text={text} /> : <Text bold>{fallback}</Text>)
  // Backticks inside inline code would end it early.
  const mono = (text: string) => (Markdown ? <Markdown text={`\`${text.replace(/`/g, "'")}\``} dimColor /> : <Text dimColor>{text}</Text>)
  const pic = (source: string, alt: string, fallback: string) =>
    Svg ? <Svg source={source} alt={alt} /> : <Text>{fallback}</Text>
  const code = (lines: readonly string[]) =>
    Code ? <Code source={lines.join('\n')} language="text" wrap="wrap" /> : <Box flexDirection="column">{lines.map(l => <Text dimColor>{l}</Text>)}</Box>
  return { Box, Text, Button, Progress, pic, code, md, mono }
}

function drawing(on: On) {
  on('ui.render', { component: 'AbovePrompt' }, async ($, e, next) => {
    if (!root || e.props.hasSurvey) return next(e)
    const { Box, Text, Button, pic } = kit($, e)
    const m = await model($)
    const openPanel = () => void $.ui.open({ id: PANE, title: 'Zitter' })

    if (m.running) {
      const p = m.running.progress
      const left = remaining(m.running, m.jobs, m.now)
      const meta = [
        p ? `${grouped(p.done)} / ${grouped(p.total)} · ${Math.floor((100 * p.done) / p.total)}%` : stopwatch(m.now - (m.running.startedAt ?? m.now)),
        left !== undefined ? minutesLeft(left) : '',
      ].filter(Boolean).join(' · ')
      return (
        <Box flexDirection="row" gap={1} alignItems="center">
          {pic(ring(p ? p.done / p.total : undefined, 16), 'Progress', '⚙')}
          <Text bold>{m.running.title}</Text>
          <Text color="inactive">{meta}</Text>
          <Box flexGrow={1} />
          <Button key="band-stop" label="Stop" onPress={() => void stop($, m.running!)} />
          <Button key="band-panel" label="Panel" onPress={openPanel} />
        </Box>
      )
    }
    if (m.failure) {
      const failed = m.failure
      return (
        <Box flexDirection="row" gap={1} alignItems="center">
          {pic(icon('cross', COLORS.error), 'Failed', '✗')}
          <Text bold>{failed.title} failed</Text>
          <Text color="inactive" wrap="truncate-end">{failed.errors?.[0] ?? `exit ${failed.exitCode}`}</Text>
          <Box flexGrow={1} />
          <Button key="band-fix" label="Ask Claude to fix" variant="primary" onPress={() => void askToFix($, failed)} />
          <Button key="band-retry" label="Retry" onPress={() => void retry($, failed)} />
          <Button key="band-dismiss" label="Dismiss" plain onPress={() => void dismiss($, failed)} />
        </Box>
      )
    }
    const fresh = freshness(m.repo)
    return (
      <Box flexDirection="row" gap={1} alignItems="center">
        {pic(dot(fresh.color, 6), 'Status', '●')}
        <Text color="inactive">Zitter · {fresh.text.split(' · ')[0]}{m.repo.head ? ` (${m.repo.head})` : ''}</Text>
        <Box flexGrow={1} />
        <Button key="band-run" label="Build & run" onPress={() => void enqueue($, 'run', 'person')} />
        <Button key="band-panel" label="Panel" onPress={openPanel} />
      </Box>
    )
  })

  on('ui.render', { component: 'Pane', requestId: PANE }, async ($, e) => {
    const { Box, Text, Button, Progress, pic, code, md, mono } = kit($, e)
    const m = await model($)
    if (!m.repo.root) return <Text color="inactive">This session is not in a Zitter checkout.</Text>

    const press = (kind: JobKind) => () => void enqueue($, kind, 'person')
    const fraction = m.running?.progress ? m.running.progress.done / m.running.progress.total : undefined
    const failed = !m.running ? m.failure : undefined
    const fresh = freshness(m.repo)
    const cols = e.props.bodyColumns

    // ---- widget: one line of state, one action
    if (e.props.placement === 'widget') {
      const r = m.running
      const left = r ? remaining(r, m.jobs, m.now) : undefined
      return (
        <Box flexDirection="row" gap={1} alignItems="center">
          {r ? pic(ring(fraction, 36), 'Progress', '⚙')
            : failed ? pic(badge('cross'), 'Failed', '✗')
            : m.last ? pic(badge('check'), 'Done', '✓')
            : pic(dot(COLORS.muted), 'Idle', '·')}
          <Box flexDirection="column" flexGrow={1}>
            <Text bold wrap="truncate-end">
              {r ? `${r.title}${fraction !== undefined ? ` · ${Math.floor(fraction * 100)}%` : ''}`
                : failed ? `${failed.title} failed${failed.errors?.length ? ` · ${failed.errors.length} errors` : ''}`
                : m.repo.appRunning ? 'App running'
                : m.last ? `${m.last.title} done` : 'Zitter'}
            </Text>
            <Text color="inactive" wrap="truncate-end">
              {r ? `${stopwatch(m.now - (r.startedAt ?? m.now))}${left !== undefined ? ` · ${minutesLeft(left)}` : ''}`
                : failed ? (failed.errors?.[0] ?? resultLine(failed))
                : m.last ? resultLine(m.last) : fresh.text}
            </Text>
          </Box>
          {r ? <Button key="w-stop" label="Stop" onPress={() => void stop($, r)} />
            : failed ? <Button key="w-fix" label="Fix" variant="primary" onPress={() => void askToFix($, failed)} />
            : <Button key="w-run" label="Build & run" variant="primary" onPress={press('run')} />}
        </Box>
      )
    }

    // ---- header
    const header = (
      <Box flexDirection="column" gap={1}>
        <Box flexDirection="row" gap={1} alignItems="center">
          {pic(icon('branch', COLORS.muted, 14), 'Branch', '')}
          <Text bold>{m.repo.branch}</Text>
          <Text color="inactive">{m.repo.head}</Text>
        </Box>
        <Box flexDirection="row" gap={1} alignItems="center">
          {pic(dot(fresh.color), 'Status', '●')}
          <Text color="inactive">{m.repo.appRunning ? `${fresh.text} · running` : fresh.text}</Text>
        </Box>
      </Box>
    )

    // ---- the card at the top: running, failed or idle
    let card
    if (m.running) {
      const r = m.running
      const p = r.progress
      const left = remaining(r, m.jobs, m.now)
      const elapsed = stopwatch(m.now - (r.startedAt ?? m.now))
      const valueText = `${elapsed}${left !== undefined ? ` · ${minutesLeft(left)}` : ''}`
      const label = p ? `${grouped(p.done)} / ${grouped(p.total)} targets` : 'Preparing'
      card = (
        <Box flexDirection="column" gap={1} borderStyle="round" borderColor="subtle" padding={1}>
          <Box flexDirection="row" alignItems="center" justifyContent="space-between">
            <Box flexDirection="column">
              <Text bold color="inactive">RUNNING</Text>
              <Text bold>{r.title}</Text>
            </Box>
            {pic(ring(fraction), fraction !== undefined ? `${Math.floor(fraction * 100)}% done` : 'Working', fraction !== undefined ? `${Math.floor(fraction * 100)}%` : '…')}
          </Box>
          {Progress
            ? <Progress label={label} value={p ? p.done : undefined} max={p ? p.total : undefined} valueText={valueText} color="claude" />
            : <Text color="inactive">{label} · {valueText}</Text>}
          {code([(r.step ?? r.tail?.at(-1) ?? 'Starting…').slice(0, Math.max(30, cols - 4))])}
          {m.shown === r.id && code((r.tail ?? []).slice(-12))}
          <Box flexDirection="row" gap={1}>
            <Button key="show-log" label={m.shown === r.id ? 'Hide log' : 'Show log'} onPress={() => void toggleShown($, r.id)} />
            <Button key="stop" label="Stop" hotkey="s" onPress={() => void stop($, r)} />
          </Box>
        </Box>
      )
    } else if (failed) {
      const lines = failed.errors?.length ? failed.errors : (failed.tail ?? []).slice(-6)
      card = (
        <Box flexDirection="column" gap={1} borderStyle="round" borderColor="error" padding={1}>
          <Box flexDirection="row" gap={1} alignItems="center">
            {pic(badge('cross'), 'Failed', '✗')}
            <Box flexDirection="column">
              <Text bold>{failed.title} failed</Text>
              <Text color="inactive">{resultLine(failed)}</Text>
            </Box>
          </Box>
          <Text bold color="inactive">
            {failed.errors?.length ? `${failed.errors.length} ERROR${failed.errors.length === 1 ? '' : 'S'}` : 'LAST LINES'}
          </Text>
          {code(lines)}
          <Button key="fix" label="Ask Claude to fix" variant="primary" onPress={() => void askToFix($, failed)} />
          <Box flexDirection="row" gap={1}>
            <Button key="retry" label="Retry" hotkey="r" onPress={() => void retry($, failed)} />
            <Button key="full-log" label="Full log" onPress={() => void openLog($, failed)} />
            <Box flexGrow={1} />
            <Button key="dismiss" label="Dismiss" plain onPress={() => void dismiss($, failed)} />
          </Box>
        </Box>
      )
    } else {
      const last = m.last
      card = (
        <Box flexDirection="column" gap={1} borderStyle="round" borderColor="subtle" padding={1}>
          {last ? (
            <Box flexDirection="row" gap={1} alignItems="center">
              {pic(badge(last.state === 'ok' ? 'check' : 'cross'), last.state === 'ok' ? 'Succeeded' : 'Stopped', last.state === 'ok' ? '✓' : '■')}
              <Box flexDirection="column">
                <Text bold>
                  {m.repo.appRunning && BUILDS.has(last.kind) ? 'App running' : `${last.title} ${last.state === 'ok' ? 'done' : 'stopped'}`}
                </Text>
                <Text color="inactive">
                  {last.title} · {formatDuration(duration(last))} · {clock(last.endedAt ?? last.queuedAt)}{last.head ? ` · from ${last.head}` : ''}
                </Text>
              </Box>
            </Box>
          ) : (
            <Text color="inactive">Build &amp; run builds the Content browser and relaunches it.</Text>
          )}
          <Button key="run" label="Build & run" hotkey="r" variant="primary" onPress={press('run')} />
          <Box flexDirection="row" gap={1}>
            <Button key="launch" label="Relaunch" hotkey="l" onPress={press('launch')} />
            {m.repo.appRunning && <Button key="quit" label="Quit app" onPress={() => void quitApp($)} />}
          </Box>
        </Box>
      )
    }

    // ---- queue
    const queue = m.queued.length > 0 && (
      <Box flexDirection="column" gap={1}>
        {m.queued.map(job => (
          <Box flexDirection="row" gap={1} alignItems="center">
            {pic(icon('clock', COLORS.muted, 14), 'Queued', '⏳')}
            <Text color="inactive">
              Next: <Text bold color="text">{job.title}</Text>
              {!m.running && m.lock.busy ? ` · waits for ${m.lock.busy}` : ''}
            </Text>
            <Box flexGrow={1} />
            <Button key={`cancel-${job.id}`} label="cancel" plain onPress={() => void stop($, job)} />
          </Box>
        ))}
      </Box>
    )

    // ---- actions
    // Idle, the card holds Build & run and Relaunch; the row keeps the rest.
    const idle = !m.running && !failed
    const actions = (
      <Box flexDirection="column" gap={1}>
        {m.running && <Button key="run" label="Build & run" hotkey="r" variant="primary" onPress={press('run')} />}
        <Box flexDirection="row" gap={1}>
          <Button key="build" label="Build" hotkey="b" onPress={press('build')} />
          {!idle && <Button key="launch" label="Launch" hotkey="l" onPress={press('launch')} />}
          <Button key="test" label="Tests" hotkey="t" onPress={press('test')} />
          {m.last && <Button key="last-log" label="Last log" onPress={() => m.last && void openLog($, m.last)} />}
        </Box>
      </Box>
    )

    // ---- build times (idle only: the place the running card takes otherwise)
    const builds = m.jobs.filter(j => isDone(j) && BUILDS.has(j.kind) && j.state !== 'stopped').slice(-10)
    const typical = median(builds.filter(j => j.state === 'ok').map(duration))
    const times = !m.running && !failed && builds.length >= 2 && (
      <Box flexDirection="column" gap={1} borderStyle="round" borderColor="subtle" padding={1}>
        <Box flexDirection="row" justifyContent="space-between">
          <Text bold color="inactive">BUILD TIMES</Text>
          {typical !== undefined && <Text color="inactive">median {formatDuration(typical)}</Text>}
        </Box>
        {pic(durationBars(builds, 340), `Durations of the last ${builds.length} builds`, builds.map(j => formatDuration(duration(j))).join(' · '))}
      </Box>
    )

    // ---- recent
    const recent = m.recent.length > 0 && (
      <Box flexDirection="column" gap={1}>
        <Box flexDirection="row" justifyContent="space-between" alignItems="center">
          <Text bold color="inactive">RECENT</Text>
          {(m.running || failed) && builds.length >= 2 && pic(durationBars(builds, 96, 22), 'Recent build times', '')}
        </Box>
        {m.recent.map(job => (
          <Box flexDirection="column">
            <Box flexDirection="row" gap={1} alignItems="center">
              {pic(icon(job.state === 'ok' ? 'check' : 'cross', job.state === 'ok' ? COLORS.success : job.state === 'failed' ? COLORS.error : COLORS.muted), job.state, job.state === 'ok' ? '✓' : job.state === 'failed' ? '✗' : '■')}
              <Button key={`job-${job.id}`} label={job.title} plain onPress={() => void toggleShown($, job.id)} />
              {job.state === 'failed' && <Text color="error">exit {job.exitCode}</Text>}
              {job.by === 'model' && <Text color="inactive">by Claude</Text>}
              <Box flexGrow={1} />
              <Text color="inactive">{formatDuration(duration(job))} · {clock(job.endedAt ?? job.queuedAt)}</Text>
            </Box>
            {m.shown === job.id && code(job.errors?.length ? job.errors : (job.tail ?? []).slice(-10))}
          </Box>
        ))}
      </Box>
    )

    // ---- footer
    const guardsText = m.approvals.length
      ? `Guards on · ${m.approvals.map(a => `${a.what.replace(':', ' ')} allowed until ${clock(a.until)}`).join(', ')}`
      : m.blocked.length ? `Guards: ${m.blocked.length} command${m.blocked.length === 1 ? '' : 's'} blocked` : 'Guards on'
    const footer = (
      <Box flexDirection="column" gap={1}>
        <Box flexDirection="row" gap={1} alignItems="center">
          {pic(dot(m.lock.busy ? COLORS.accent : COLORS.success, 6), 'Build dir', '●')}
          <Text color="inactive" wrap="truncate-end">
            {m.lock.busy ? (m.lock.busy.startsWith("this session's") ? 'Build dir held by this session' : `Build dir busy · ${m.lock.busy}`) : 'Build dir free'}
          </Text>
          <Box flexGrow={1} />
          {m.repo.diskFree && <Text color="inactive">{m.repo.diskFree} free</Text>}
        </Box>
        <Box flexDirection="row" gap={1} alignItems="center">
          {pic(icon('shield', COLORS.muted, 12), 'Guards', '')}
          <Text color="inactive" wrap="truncate-end">{guardsText}</Text>
          <Box flexGrow={1} />
          {m.blocked.length > 0 && <Button key="guards" label={m.shown === 'guards' ? 'hide' : 'details'} plain onPress={() => void toggleShown($, 'guards')} />}
        </Box>
        {m.shown === 'guards' && m.blocked.slice(-5).reverse().map(b => (
          <Text color="inactive" wrap="truncate-end">
            {clock(b.at)} {(b.command.split('\n')[0] ?? '').slice(0, Math.max(20, cols - 8))}
          </Text>
        ))}
      </Box>
    )

    return (
      <Box flexDirection="column" gap={1}>
        {header}
        {card}
        {queue}
        {actions}
        {times}
        {recent}
        {footer}
      </Box>
    )
  })
}
