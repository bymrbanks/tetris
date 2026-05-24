// Neon HTTP-driver client — singleton per cold start.
import { neon } from '@neondatabase/serverless';

let _sql = null;

export function sql() {
  if (!_sql) {
    if (!process.env.DATABASE_URL) {
      throw new Error('DATABASE_URL env var not set');
    }
    _sql = neon(process.env.DATABASE_URL);
  }
  return _sql;
}
