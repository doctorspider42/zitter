// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
export type JobKind = 'build' | 'run' | 'launch' | 'full' | 'test' | 'uitest' | 'content' | 'content-run'

export type JobState = 'queued' | 'running' | 'ok' | 'failed' | 'stopped'

export type Job = {
  id: string
  kind: JobKind
  title: string
  state: JobState
  /** Who asked: the person (command, pane) or the model (tool). */
  by: 'person' | 'model'
  filter?: string
  queuedAt: number
  startedAt?: number
  endedAt?: number
  pid?: number
  exitCode?: number
  /** The commit the job built from. */
  head?: string
  log?: string
  progress?: { done: number; total: number }
  tail?: string[]
  /** The ninja edge running now (`CXX obj/…/x.o`). */
  step?: string
  /** Error lines of a failed job, from its log. */
  errors?: string[]
}

export type Lock = {
  /** What holds the shared build directory, when something does. */
  busy?: string
  checkedAt: number
}

export type Repo = {
  root: string
  branch: string
  head: string
  dirty: number
  /** The commit the last successful app build came from. */
  builtHead?: string
  /** Commits between builtHead and head, when known. */
  behind?: number
  diskFree?: string
  /** When build/ZitterRustMetal.app was last packaged (ms). */
  appBuiltAt?: number
  /** Whether the packaged dev app is running now. */
  appRunning?: boolean
}

export type Approval = { what: string; until: number }

export type Blocked = { at: number; command: string; reason: string }

declare module 'claude-code' {
  interface PluginState {
    'zitter-dev': {
      jobs: Shaped<Job[]>
      lock: Lock
      repo: Repo
      approvals: Approval[]
      blocked: Blocked[]
      shownLog: string
      autoOpened: boolean
      /** The failed job whose card the person dismissed. */
      dismissed: string
    }
  }
}
