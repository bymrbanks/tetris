// GET /api/challenges/:slug — one challenge + its top-10 winners.
// Winners only revealed after ends_at, before that returns an empty winners array.

import { sql } from '../_lib/db.js';

export default async function handler(req, res) {
  if (req.method !== 'GET') {
    res.setHeader('Allow', 'GET');
    return res.status(405).json({ error: 'method not allowed' });
  }

  const slug = String(req.query.slug || '');
  if (!slug) return res.status(400).json({ error: 'slug required' });

  const db = sql();
  const chs = await db`
    SELECT id, slug, title, description, prize, starts_at, ends_at
    FROM challenges WHERE slug = ${slug} LIMIT 1
  `;
  if (!chs[0]) return res.status(404).json({ error: 'not found' });
  const ch = chs[0];

  const ended = new Date(ch.ends_at) <= new Date();
  let winners = [];
  if (ended) {
    winners = await db`
      SELECT name, score, lines, level, created_at, country
      FROM scores WHERE challenge_id = ${ch.id}
      ORDER BY score DESC, created_at ASC
      LIMIT 10
    `;
  }

  res.setHeader('Cache-Control', ended ? 'public, max-age=300' : 's-maxage=60');
  return res.status(200).json({ ...ch, ended, winners });
}
