// GET  /api/challenges?active=1   — list active (or all) challenges
// POST /api/challenges              — create one (header: x-admin-token)

import { sql } from './_lib/db.js';
import { isAdmin } from './_lib/auth.js';
import { validateChallenge } from './_lib/validate.js';

export default async function handler(req, res) {
  const db = sql();

  if (req.method === 'GET') {
    const active = req.query.active === '1';
    const rows = active
      ? await db`
          SELECT id, slug, title, description, prize, starts_at, ends_at
          FROM challenges
          WHERE now() < ends_at
          ORDER BY starts_at ASC`
      : await db`
          SELECT id, slug, title, description, prize, starts_at, ends_at
          FROM challenges
          ORDER BY starts_at DESC`;
    res.setHeader('Cache-Control', 's-maxage=60, stale-while-revalidate=120');
    return res.status(200).json(rows);
  }

  if (req.method === 'POST') {
    if (!isAdmin(req)) return res.status(401).json({ error: 'admin token required' });

    let body = req.body;
    if (typeof body === 'string') {
      try { body = JSON.parse(body); } catch { return res.status(400).json({ error: 'invalid JSON' }); }
    }
    const v = validateChallenge(body);
    if (!v.ok) return res.status(400).json({ error: v.error });

    try {
      const rows = await db`
        INSERT INTO challenges (slug, title, description, prize, starts_at, ends_at)
        VALUES (${body.slug}, ${body.title}, ${body.description || null}, ${body.prize || null},
                ${body.starts_at}::timestamptz, ${body.ends_at}::timestamptz)
        RETURNING id, slug, title, description, prize, starts_at, ends_at
      `;
      return res.status(201).json(rows[0]);
    } catch (err) {
      const msg = err?.message || '';
      if (msg.includes('challenges_slug_key') || msg.includes('duplicate key')) {
        return res.status(409).json({ error: 'slug already exists' });
      }
      if (msg.includes('challenges_starts_at_ends_at_excl') || msg.includes('overlap')) {
        return res.status(409).json({ error: 'overlaps an existing challenge' });
      }
      console.error('challenges insert failed:', err);
      return res.status(500).json({ error: 'database error' });
    }
  }

  res.setHeader('Allow', 'GET, POST');
  return res.status(405).json({ error: 'method not allowed' });
}
