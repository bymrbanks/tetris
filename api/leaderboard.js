// GET /api/leaderboard?period=all|week|month|challenge&slug=<slug>&limit=20
// Returns top entries for the given period.

import { sql } from './_lib/db.js';

export default async function handler(req, res) {
  if (req.method !== 'GET') {
    res.setHeader('Allow', 'GET');
    return res.status(405).json({ error: 'method not allowed' });
  }

  const period = String(req.query.period || 'all');
  const slug   = req.query.slug ? String(req.query.slug) : null;
  let limit = parseInt(req.query.limit ?? '20', 10);
  if (!Number.isFinite(limit) || limit < 1) limit = 20;
  if (limit > 100) limit = 100;

  const db = sql();

  let rows;
  try {
    if (period === 'all') {
      rows = await db`
        SELECT id, name, score, lines, level, created_at, country, client_id
        FROM scores
        ORDER BY score DESC, created_at ASC
        LIMIT ${limit}`;
    } else if (period === 'week') {
      rows = await db`
        SELECT id, name, score, lines, level, created_at, country, client_id
        FROM scores
        WHERE created_at >= date_trunc('week', now())
        ORDER BY score DESC, created_at ASC
        LIMIT ${limit}`;
    } else if (period === 'month') {
      rows = await db`
        SELECT id, name, score, lines, level, created_at, country, client_id
        FROM scores
        WHERE created_at >= date_trunc('month', now())
        ORDER BY score DESC, created_at ASC
        LIMIT ${limit}`;
    } else if (period === 'challenge') {
      if (!slug) return res.status(400).json({ error: 'slug required for challenge period' });
      const ch = await db`SELECT id FROM challenges WHERE slug = ${slug} LIMIT 1`;
      if (!ch[0]) return res.status(404).json({ error: 'challenge not found' });
      rows = await db`
        SELECT id, name, score, lines, level, created_at, country, client_id
        FROM scores
        WHERE challenge_id = ${ch[0].id}
        ORDER BY score DESC, created_at ASC
        LIMIT ${limit}`;
    } else {
      return res.status(400).json({ error: 'period must be all|week|month|challenge' });
    }
  } catch (err) {
    console.error('leaderboard query failed:', err);
    return res.status(500).json({ error: 'database error' });
  }

  const entries = rows.map((r, i) => ({
    rank:       i + 1,
    name:       r.name,
    score:      r.score,
    lines:      r.lines,
    level:      r.level,
    created_at: r.created_at,
    country:    r.country,
    client_id:  r.client_id,
  }));

  res.setHeader('Cache-Control', 's-maxage=30, stale-while-revalidate=60');
  return res.status(200).json({ scope: period, slug: slug || null, entries });
}
