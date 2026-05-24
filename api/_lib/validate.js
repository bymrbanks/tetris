// Submit-payload + challenge-payload validation.
// Mirrors the DB CHECK constraints plus plausibility heuristics that the DB can't enforce.

import { normalizeName, isBlocked } from './profanity.js';

export function validateSubmit(body) {
  if (typeof body !== 'object' || !body) return { ok: false, error: 'body required' };

  // Shape checks
  if (typeof body.name !== 'string')        return { ok: false, error: 'name required (string)' };
  if (!Number.isInteger(body.score))        return { ok: false, error: 'score must be integer' };
  if (!Number.isInteger(body.lines))        return { ok: false, error: 'lines must be integer' };
  if (!Number.isInteger(body.level))        return { ok: false, error: 'level must be integer' };
  if (!Number.isInteger(body.durationMs))   return { ok: false, error: 'durationMs must be integer' };
  if (typeof body.clientId !== 'string' || !/^[0-9a-f-]{36}$/i.test(body.clientId)) {
    return { ok: false, error: 'clientId must be uuid' };
  }

  // Name normalize + blocklist
  const name = normalizeName(body.name);
  if (!name) return { ok: false, error: 'name not allowed (use letters/numbers, 1–16 chars)' };
  if (isBlocked(name)) return { ok: false, error: 'name not allowed' };

  // Range checks (mirror DB CHECKs)
  if (body.score < 0 || body.score > 10_000_000)              return { ok: false, error: 'score out of range' };
  if (body.lines < 0 || body.lines > 100_000)                 return { ok: false, error: 'lines out of range' };
  if (body.level < 1 || body.level > 100)                     return { ok: false, error: 'level out of range' };
  if (body.durationMs < 1000 || body.durationMs > 86_400_000) return { ok: false, error: 'duration out of range' };

  // Plausibility: score ≤ permissive max derived from lines+level.
  // Real Tetris max per piece ≈ 800 * level (Tetris clear) + ~40 drop bonus.
  if (body.lines > 0) {
    const maxScore = body.lines * 250 * body.level + body.lines * 40 + 1000;
    if (body.score > maxScore) return { ok: false, error: 'score implausible for lines/level' };
  } else if (body.score > 1000) {
    return { ok: false, error: 'score implausible for zero lines' };
  }

  // Level should match lines/10+1 within ±1 (race on the threshold tick)
  const expectedLevel = Math.floor(body.lines / 10) + 1;
  if (Math.abs(body.level - expectedLevel) > 1) {
    return { ok: false, error: 'level inconsistent with lines' };
  }

  // Score per ms: real play is 1–5 pts/ms; cap generously at 50
  if (body.score / body.durationMs > 50) {
    return { ok: false, error: 'score/duration ratio implausible' };
  }

  return { ok: true, name };
}

export function validateChallenge(body) {
  if (typeof body !== 'object' || !body) return { ok: false, error: 'body required' };
  if (typeof body.slug !== 'string' || !/^[a-z0-9-]{1,32}$/i.test(body.slug)) {
    return { ok: false, error: 'slug must match [a-z0-9-]{1,32}' };
  }
  if (typeof body.title !== 'string' || body.title.length < 1 || body.title.length > 80) {
    return { ok: false, error: 'title 1-80 chars' };
  }
  if (typeof body.starts_at !== 'string' || isNaN(Date.parse(body.starts_at))) {
    return { ok: false, error: 'starts_at ISO datetime required' };
  }
  if (typeof body.ends_at !== 'string' || isNaN(Date.parse(body.ends_at))) {
    return { ok: false, error: 'ends_at ISO datetime required' };
  }
  if (Date.parse(body.ends_at) <= Date.parse(body.starts_at)) {
    return { ok: false, error: 'ends_at must be after starts_at' };
  }
  if (body.description && typeof body.description !== 'string') {
    return { ok: false, error: 'description must be string' };
  }
  if (body.prize && typeof body.prize !== 'string') {
    return { ok: false, error: 'prize must be string' };
  }
  return { ok: true };
}
