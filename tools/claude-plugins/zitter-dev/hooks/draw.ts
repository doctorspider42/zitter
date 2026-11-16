// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
// Pure helpers for the panel: inline SVG markup and the numbers it shows.
// SVG is drawn as an image, so it cannot follow the app's theme keys; these
// mid-tone colors read on both the light and the dark background.

import type { Job } from '../types'

export const COLORS = {
  accent: '#D97757',
  track: 'rgba(138,136,128,0.28)',
  muted: '#8A8880',
  bar: 'rgba(138,136,128,0.45)',
  success: '#3F9A62',
  error: '#D2493F',
}

export type IconName = 'check' | 'cross' | 'branch' | 'clock' | 'shield' | 'spark'

const PATHS: Record<IconName, string> = {
  check: '<path d="M5 12l5 5L19 7"/>',
  cross: '<path d="M6 6l12 12M18 6L6 18"/>',
  branch: '<circle cx="6" cy="5" r="2"/><circle cx="6" cy="19" r="2"/><circle cx="18" cy="7" r="2"/><path d="M6 7v10M18 9c0 5-12 3-12 8"/>',
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  shield: '<path d="M12 3l8 3v6c0 4.5-3.4 8-8 9-4.6-1-8-4.5-8-9V6z"/>',
  spark: '<path d="M12 3l1.8 5.2L19 10l-5.2 1.8L12 17l-1.8-5.2L5 10l5.2-1.8z"/>',
}

export function icon(name: IconName, color: string, size = 16): string {
  const width = name === 'check' || name === 'cross' ? 2.5 : 2
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="${width}" stroke-linecap="round" stroke-linejoin="round">${PATHS[name]}</svg>`
}

/** A disc with an icon in it: the result badge of a finished job. */
export function badge(name: 'check' | 'cross', size = 36): string {
  const color = name === 'check' ? COLORS.success : COLORS.error
  const inner = Math.round(size / 2)
  const offset = (size - inner) / 2
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 ${size} ${size}"><circle cx="${size / 2}" cy="${size / 2}" r="${size / 2}" fill="${color}" fill-opacity="0.16"/><svg x="${offset}" y="${offset}" width="${inner}" height="${inner}" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">${PATHS[name]}</svg></svg>`
}

/** A progress ring; an open quarter arc while the end is unknown. */
export function ring(fraction: number | undefined, size = 44): string {
  const r = 18
  const c = 2 * Math.PI * r
  const f = fraction === undefined ? 0.25 : Math.max(0.02, Math.min(1, fraction))
  const dash = `${(c * f).toFixed(1)} ${c.toFixed(1)}`
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 44 44"><circle cx="22" cy="22" r="${r}" fill="none" stroke="${COLORS.track}" stroke-width="4.5"/><circle cx="22" cy="22" r="${r}" fill="none" stroke="${COLORS.accent}" stroke-width="4.5" stroke-linecap="round" stroke-dasharray="${dash}" transform="rotate(-90 22 22)"/></svg>`
}

export function dot(color: string, size = 8): string {
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 8 8"><circle cx="4" cy="4" r="4" fill="${color}"/></svg>`
}

/** Bars of the durations of recent jobs, oldest first; the latest in the accent color. */
export function durationBars(jobs: readonly Job[], width: number, height = 56): string {
  const values = jobs.map(duration)
  const max = Math.max(...values, 1)
  const n = values.length
  const gap = 8
  const w = Math.max(6, Math.floor((width - gap * (n - 1)) / Math.max(n, 1)))
  const bars = values
    .map((v, i) => {
      const h = Math.max(4, Math.round(((height - 2) * v) / max))
      const job = jobs[i]!
      const fill = job.state === 'failed' ? COLORS.error : i === n - 1 ? COLORS.accent : COLORS.bar
      return `<rect x="${i * (w + gap)}" y="${height - 1 - h}" width="${w}" height="${h}" rx="3" fill="${fill}"/>`
    })
    .join('')
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}"><line x1="0" y1="${height - 0.5}" x2="${width}" y2="${height - 0.5}" stroke="${COLORS.track}"/>${bars}</svg>`
}

export const duration = (job: Job) => Math.max(0, (job.endedAt ?? job.queuedAt) - (job.startedAt ?? job.queuedAt))

export function median(values: readonly number[]): number | undefined {
  if (values.length === 0) return
  const sorted = [...values].sort((a, b) => a - b)
  const mid = Math.floor(sorted.length / 2)
  return sorted.length % 2 ? sorted[mid] : ((sorted[mid - 1] ?? 0) + (sorted[mid] ?? 0)) / 2
}

/** How long a running job has left: from ninja's own pace once it is under way, else from past runs of its kind. */
export function remaining(job: Job, history: readonly Job[], now: number): number | undefined {
  const elapsed = now - (job.startedAt ?? now)
  const p = job.progress
  if (p && p.total > 0 && p.done / p.total >= 0.1) {
    return Math.max(0, (elapsed * p.total) / p.done - elapsed)
  }
  const past = history.filter(j => j.kind === job.kind && j.state === 'ok').slice(-5).map(duration)
  const typical = median(past)
  return typical === undefined ? undefined : Math.max(0, typical - elapsed)
}

/** "12 340" with thin grouping, the way the panel shows target counts. */
export const grouped = (n: number) => String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ' ')

/** "4:12", minutes and seconds of a running clock. */
export function stopwatch(ms: number): string {
  const s = Math.max(0, Math.floor(ms / 1000))
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  const ss = String(s % 60).padStart(2, '0')
  return h ? `${h}:${String(m).padStart(2, '0')}:${ss}` : `${m}:${ss}`
}

export const minutesLeft = (ms: number) => (ms < 60_000 ? '<1 min left' : `~${Math.round(ms / 60_000)} min left`)
