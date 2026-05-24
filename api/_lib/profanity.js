// Name normalize + tiny starter blocklist.
// Extend BLOCKED with more terms as needed; this is intentionally small.

// Allowed: letters, digits, space, underscore, period, hyphen. 1-16 chars.
// Unicode \p{L}\p{N} rejects control characters and most punctuation.
const NAME_RE = /^[\p{L}\p{N} _.\-]{1,16}$/u;

const BLOCKED = new Set([
  'admin', 'root', 'null', 'undefined', 'system', 'mod', 'moderator',
  'fuck', 'shit', 'bitch', 'asshole', 'nigger', 'nigga', 'faggot', 'retard', 'cunt',
]);

export function normalizeName(s) {
  if (typeof s !== 'string') return '';
  // NFKC normalize + trim whitespace. NAME_RE handles the rest of the filtering.
  let n = s.normalize('NFKC').trim();
  if (n.length > 16) n = n.slice(0, 16);
  if (!NAME_RE.test(n)) return '';
  return n;
}

export function isBlocked(name) {
  if (!name) return true;
  // Compare on letters+digits only so obvious obfuscation ("f.u.c.k") still trips.
  const stripped = name.toLowerCase().replace(/[^a-z0-9]/g, '');
  return BLOCKED.has(stripped);
}
