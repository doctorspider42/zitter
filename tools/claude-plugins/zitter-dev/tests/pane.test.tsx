// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
import { expect, test } from 'claude-code/testing'

const PANE = {
  plugin: 'zitter-dev',
  component: 'Pane',
  requestId: 'zitter',
  props: {
    title: 'Zitter',
    isFocused: false,
    bodyColumns: 60,
    placement: 'dock',
    scroll: { offset: 0, bodyRows: 40 },
    view: {},
  },
} as const

const job = (over: Record<string, unknown>) => ({
  id: 'j1', kind: 'run', title: 'Build & run', state: 'ok', by: 'person', queuedAt: 1, startedAt: 1, endedAt: 61_000, ...over,
})

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function seed(on: any, values: Record<string, unknown>) {
  on('state.get', (_: unknown, e: unknown) => {
    const key = (e as { key: string }).key
    return { value: key in values ? { value: values[key], version: 1 } : { value: undefined, version: 0 } }
  })
}

const REPO = { root: '/z', branch: 'main', head: 'abc1234', dirty: 2, builtHead: 'abc1234', behind: 0, diskFree: '100Gi' }

test('the panel shows the repo, the buttons and a running job, on every surface', async ($, on) => {
  seed(on, {
    repo: REPO,
    lock: { busy: "this session's Build & run (j2)", checkedAt: 0 },
    jobs: { shape: 'jobs-2', value: [job({}), job({ id: 'j2', state: 'running', progress: { done: 50, total: 100 }, tail: ['[50/100] CXX x.o'] })] },
  })
  for (const surface of ['terminal', 'desktop', 'vscode', 'mobile'] as const) {
    const ui = await $.ui.mount({ ...PANE, surface })
    expect((await ui.find({ type: 'Text', text: /up to date/ }))).toBeDefined()
    expect(await ui.find({ key: 'run' })).toBeDefined()
    expect(await ui.find({ key: 'stop' })).toBeDefined()
    expect(await ui.find({ type: 'Text', text: /50 \/ 100 targets|Build & run/ })).toBeDefined()
    await ui.unmount()
  }
})

test('outside a Zitter checkout the panel says so', async ($, on) => {
  seed(on, {})
  const ui = await $.ui.mount({ ...PANE, surface: 'desktop' })
  expect((await ui.find({ type: 'Text' }))?.text).toBe('This session is not in a Zitter checkout.')
  await ui.unmount()
})

test('the widget offers Build & run when idle', async ($, on) => {
  seed(on, { repo: REPO, jobs: { shape: 'jobs-2', value: [job({})] } })
  const ui = await $.ui.mount({ ...PANE, props: { ...PANE.props, placement: 'widget' }, surface: 'desktop' })
  expect(await ui.find({ key: 'w-run' })).toBeDefined()
  expect(await ui.find({ type: 'Text', text: /Build & run done/ })).toBeDefined()
  await ui.unmount()
})

test('a failed build offers the fix and shows its error lines, on every surface', async ($, on) => {
  seed(on, {
    repo: REPO,
    lock: { checkedAt: 0 },
    jobs: { shape: 'jobs-2', value: [job({ state: 'failed', exitCode: 1, errors: ["shell.cc:412:7: error: no member named 'SetPinned'"] })] },
  })
  for (const surface of ['terminal', 'desktop', 'vscode', 'mobile'] as const) {
    const ui = await $.ui.mount({ ...PANE, surface })
    expect(await ui.find({ key: 'fix' })).toBeDefined()
    expect(await ui.find({ key: 'retry' })).toBeDefined()
    expect(await ui.find({ key: 'dismiss' })).toBeDefined()
    expect(await ui.find({ type: 'Text', text: /Build & run failed/ })).toBeDefined()
    await ui.unmount()
  }
})

test('a dismissed failure gives the card back to Build & run', async ($, on) => {
  seed(on, {
    repo: REPO,
    dismissed: 'j1',
    jobs: { shape: 'jobs-2', value: [job({ state: 'failed', exitCode: 1 })] },
  })
  const ui = await $.ui.mount({ ...PANE, surface: 'desktop' })
  expect(await ui.find({ key: 'fix' })).toBeUndefined()
  expect(await ui.find({ key: 'run' })).toBeDefined()
  await ui.unmount()
})
