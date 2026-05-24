// POST /api/scores — submit a new score.
// Body: { name, score, lines, level, durationMs, clientId }
// Returns: 201 { id, rank: { allTime, weekly, monthly, challenge? } }

import { sql } from './_lib/db.js';
import { validateSubmit } from './_lib/validate.js';
import { ipHash, clientIp, checkRateLimit } from './_lib/ratelimit.js';

export default async function handler(req, res) {
  if (req.method !== 'POST') {
    res.setHeader('Allow', 'POST');
    return res.status(405).json({ error: 'method not allowed' });
  }

  // Parse body (Vercel parses JSON automatically, but be defensive)
  let body = req.body;
  if (typeof body === 'string') {
    try { body = JSON.parse(body); } catch { return res.status(400).json({ error: 'invalid JSON' }); }
  }

  const v = validateSubmit(body);
  if (!v.ok) return res.status(400).json({ error: v.error });

  const ipHashBuf = ipHash(clientIp(req));
  const rl = await checkRateLimit({ ipHashBuf, clientId: body.clientId });
  if (!rl.ok) return res.status(429).json({ error: rl.reason });

  const db = sql();

  // Stamp challenge_id if a named challenge is currently active
  const activeCh = await db`
    SELECT id FROM challenges
    WHERE now() >= starts_at AND now() < ends_at
    ORDER BY starts_at DESC LIMIT 1
  `;
  const challengeId = activeCh[0]?.id || null;

  const country   = req.headers['x-vercel-ip-country'] || null;
  const userAgent = req.headers['user-agent'] || null;

  let id;
  try {
    const ins = await db`
      INSERT INTO scores (name, score, lines, level, duration_ms,
                          client_id, ip_hash, user_agent, country, challenge_id)
      VALUES (${v.name}, ${body.score}, ${body.lines}, ${body.level}, ${body.durationMs},
              ${body.clientId}::uuid, ${ipHashBuf}, ${userAgent}, ${country}, ${challengeId})
      RETURNING id
    `;
    id = ins[0].id;
  } catch (err) {
    console.error('scores insert failed:', err);
    return res.status(500).json({ error: 'database error' });
  }

  // Ranks — count strictly-better scores + 1
  const [allRow]   = await db`SELECT count(*)::int + 1 AS rank FROM scores WHERE score > ${body.score}`;
  const [weekRow]  = await db`SELECT count(*)::int + 1 AS rank FROM scores WHERE score > ${body.score} AND created_at >= date_trunc('week',  now())`;
  const [monthRow] = await db`SELECT count(*)::int + 1 AS rank FROM scores WHERE score > ${body.score} AND created_at >= date_trunc('month', now())`;
  let challengeRank = null;
  if (challengeId) {
    const [chRow] = await db`SELECT count(*)::int + 1 AS rank FROM scores WHERE score > ${body.score} AND challenge_id = ${challengeId}`;
    challengeRank = chRow.rank;
  }

  res.setHeader('Cache-Control', 'no-store');
  return res.status(201).json({
    id,
    rank: {
      allTime:   allRow.rank,
      weekly:    weekRow.rank,
      monthly:   monthRow.rank,
      challenge: challengeRank
    }
  });
}
