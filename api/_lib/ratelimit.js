// Rate-limiting + IP hashing for score submissions.
import { createHash } from 'node:crypto';
import { sql } from './db.js';

export function ipHash(ip) {
  const salt = process.env.IP_HASH_SALT || 'unsalted-dev';
  return createHash('sha256').update(String(ip || '') + ':' + salt).digest();
}

export function clientIp(req) {
  const xff = req.headers['x-forwarded-for'];
  if (xff) return String(xff).split(',')[0].trim();
  if (req.headers['x-real-ip']) return String(req.headers['x-real-ip']);
  return (req.socket && req.socket.remoteAddress) || '';
}

export async function checkRateLimit({ ipHashBuf, clientId }) {
  const db = sql();

  // Same IP > 5 submits in 60s
  const ipRows = await db`
    SELECT count(*)::int AS n FROM scores
    WHERE ip_hash = ${ipHashBuf}
      AND created_at >= now() - interval '60 seconds'
  `;
  if (ipRows[0].n >= 5) {
    return { ok: false, reason: 'too many submissions from this address' };
  }

  // Same clientId > 2 in 10s
  const cidRows = await db`
    SELECT count(*)::int AS n FROM scores
    WHERE client_id = ${clientId}::uuid
      AND created_at >= now() - interval '10 seconds'
  `;
  if (cidRows[0].n >= 2) {
    return { ok: false, reason: 'slow down — please wait before submitting again' };
  }

  return { ok: true };
}
