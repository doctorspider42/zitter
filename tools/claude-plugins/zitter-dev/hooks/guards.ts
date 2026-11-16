// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Hard rules for the Bash tool in the Zitter repository. Each rule is a plain
// function of the command text and a context, so it can be tested without an
// engine. A rule returns why it refuses the command, or undefined.

export type GuardContext = {
  /** Active approvals the person gave with /dev-allow: `outdir:<name>`, `delete-out:<name>`. */
  approvals: readonly string[]
  /** Names of the existing `.chromium/src/out/*` directories. */
  outDirs: readonly string[]
  /** A running build that holds the shared build directory, if any. */
  busy?: string
  /** The host; rules that only bite on one platform check it. macOS when absent. */
  platform?: 'macos' | 'windows'
  /** The dev output directory of this platform, for messages. */
  outDir?: string
}

// Where a command word may start: the line's start, after a separator or a
// subshell, past env assignments and wrappers (`FOO=1 caffeinate -i ./x`).
// Backticks are left out on purpose: in commands they are rare, while in
// heredocs and comments `ninja` quoted in backticks is common prose.
const START = String.raw`(?:^|[;&|(]\s*|\$\(\s*)(?:(?:[A-Za-z_][A-Za-z0-9_]*=\S*|env|exec|time|nice|nohup|caffeinate(?:\s+-\w+)*|zsh|bash|sh|cmd\s+/c|powershell(?:\s+-\w+)*|pwsh(?:\s+-\w+)*|&)\s+)*`
const END = String.raw`(?=[\s;&|)]|$)`

const at = (word: string) => new RegExp(START + word + END, 'mi')

/**
 * Heredoc bodies are data (a commit message, a file being written), not
 * commands: they are dropped before any rule reads the command.
 */
export function stripHeredocs(command: string): string {
  const lines = command.split('\n')
  const kept: string[] = []
  let end: string | undefined
  for (const line of lines) {
    if (end !== undefined) {
      if (line.replace(/^\t+/, '') === end) end = undefined
      continue
    }
    kept.push(line)
    const marker = /<<-?\s*(['"]?)([A-Za-z_][A-Za-z0-9_]*)\1/.exec(line)
    if (marker) end = marker[2]
  }
  return kept.join('\n')
}

/** Windows paths read as POSIX ones, so one set of patterns serves both. */
/**
 * Quoted arguments that span text (a commit message, a PR title) are data too:
 * a double- or single-quoted string with a space or newline in it is blanked.
 * A quoted path or a quoted flag has no space and stays.
 */
export const stripQuotedText = (command: string) =>
  command.replace(/"(?:[^"\\]|\\.)*"|'[^']*'/g, quoted => (/\s/.test(quoted.slice(1, -1)) ? '""' : quoted))

const normalize = (command: string) => stripQuotedText(stripHeredocs(command)).replace(/\\/g, '/')

/** The segments of a command line: split on separators, newlines and pipes. */
const segments = (command: string) => command.split(/\n|;|&&|\|\||\|/).map(s => s.trim()).filter(Boolean)

const unquote = (s: string) => s.replace(/^['"]|['"]$/g, '')

const outName = (token: string): string | undefined =>
  /(?:^|\/)out\/([^/\s"']+)\/?$/.exec(unquote(token))?.[1]

export const BUILD_ENTRY = at(String.raw`(?:\S*/)?(?:build\.sh|build_app\.sh|build_app_win\.ps1)`)
const BUILD_LIKE = at(
  String.raw`(?:\S*third_party/ninja/ninja(?:\.exe)?|autoninja(?:\.bat)?|python3?\s+\S*stage_content_embedder\.py|\S*stage_content_embedder\.py|\S*buildtools/(?:mac|win)/gn(?:\.exe)?\s+gen|gn\s+gen|(?:\S*/)?(?:build\.sh|build_app\.sh|build_app_win\.ps1))`,
)

function homebrewNinja(command: string, ctx: GuardContext): string | undefined {
  if (ctx.platform === 'windows') return
  if (!at(String.raw`(?:/opt/homebrew/bin/|/usr/local/bin/)?ninja`).test(command)) return
  return 'A bare `ninja` resolves to Homebrew\'s, which wipes the v6 build log of a Chromium out dir. Use .chromium/src/third_party/ninja/ninja, or zitter_dev_start.'
}

function newOutDir(command: string, ctx: GuardContext): string | undefined {
  const names: string[] = []
  for (const m of command.matchAll(/\bgn(?:\.exe)?\s+gen\s+(?:--\S+\s+)*(\S+)/g)) {
    // A variable (`"$OUT"`) cannot be resolved here and passes.
    const name = outName(m[1] ?? '')
    if (name) names.push(name)
  }
  for (const m of command.matchAll(/\b(?:mkdir|md|New-Item)\s+((?:-\w+(?:\s+\w+)?\s+)*)(\S+)/gi)) {
    const name = outName(m[2] ?? '')
    if (name) names.push(name)
  }
  for (const name of names) {
    if (ctx.outDirs.includes(name) || ctx.approvals.includes(`outdir:${name}`)) continue
    return `Creating the Chromium output directory out/${name} needs the person's OK: it takes 10–40 GB and hours to build and to delete. Ask them with the expected size and build time; they approve with /dev-allow outdir ${name}. Prefer the existing ${ctx.outDir ?? 'out/ZitterContentBundle'}.`
  }
}

/** The paths a recursive delete names: `rm -r`, `rmdir /s`, `Remove-Item -Recurse`, `rd /s`. */
function deletedPaths(segment: string): string[] {
  const rm = /(?:^|\s)rm\s+(.*)$/.exec(segment)?.[1]
  if (rm && /(^|\s)-\w*[rR]/.test(rm)) return rm.split(/\s+/).filter(t => !t.startsWith('-'))
  const rd = /(?:^|\s)(?:rmdir|rd)\s+(.*)$/i.exec(segment)?.[1]
  // Flags are `/s`, `/q`: one letter after the slash; anything longer is a path.
  if (rd && /(^|\s)\/s\b/i.test(rd)) return rd.split(/\s+/).filter(t => !/^\/[a-z]$/i.test(t))
  const ri = /(?:^|\s)(?:Remove-Item|ri|del|erase)\s+(.*)$/i.exec(segment)?.[1]
  if (ri && /-Recurse\b|-r\b|\/s\b/i.test(ri)) return ri.split(/\s+/).filter(t => !t.startsWith('-') && !/^\/[sq]$/i.test(t))
  return []
}

function deletion(command: string, ctx: GuardContext): string | undefined {
  if (/\brobocopy\b[^\n]*\/MIR\b/i.test(command)) {
    return 'Never delete with robocopy /MIR: it follows junctions and once wiped the Chromium checkout. Use `cmd /c rmdir /s /q <dir>`.'
  }
  for (const segment of segments(command)) {
    for (const token of deletedPaths(segment)) {
      const path = unquote(token).replace(/\/+$/, '')
      if (/(^|\/)\.chromium(\/src)?$/.test(path)) {
        return 'Never delete the Chromium checkout (.chromium). That is the person\'s call alone.'
      }
      const name = outName(path)
      if (name && /(^|\/)(\.chromium\/src\/)?out\/[^/]+$/.test(path) && !ctx.approvals.includes(`delete-out:${name}`)) {
        return `Deleting out/${name} takes hours on a Chromium output directory and is the person's call: ask them first; they approve with /dev-allow delete-out ${name}.`
      }
    }
  }
}

function concurrentBuild(command: string, ctx: GuardContext): string | undefined {
  if (!ctx.busy || !BUILD_LIKE.test(command)) return
  return `Another build is using the shared build directory (.chromium/src/${ctx.outDir ?? 'out/ZitterContentBundle'}): ${ctx.busy}. Do not stage or run ninja now; wait for it with zitter_dev_wait, or queue your build with zitter_dev_start.`
}

function directBuild(command: string): string | undefined {
  if (!BUILD_ENTRY.test(command) || /\bZITTER_DEV_DIRECT=1\b/.test(command)) return
  if (/--(plan|help)\b/.test(command)) return
  return 'Builds go through zitter-dev so they run detached, queue on the shared build directory and show progress to the person. Call mcp__zitter-dev__zitter_dev_start (kind: build, run, launch, test), then mcp__zitter-dev__zitter_dev_wait. Prefix ZITTER_DEV_DIRECT=1 only when the person asked for a direct run.'
}

/** Why the Bash command is refused here, or undefined when it may run. */
export function checkCommand(raw: string, ctx: GuardContext): string | undefined {
  const command = normalize(raw)
  return (
    homebrewNinja(command, ctx) ??
    newOutDir(command, ctx) ??
    deletion(command, ctx) ??
    concurrentBuild(command, ctx) ??
    directBuild(command)
  )
}

/** Whether a command would use the shared build directory (so it needs the lock). */
export const isBuildCommand = (command: string) => BUILD_LIKE.test(normalize(command))
