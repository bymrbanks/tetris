-- Tetris global leaderboard schema (Neon Postgres).
-- Apply with: psql "$DATABASE_URL" -f db/schema.sql

CREATE EXTENSION IF NOT EXISTS citext;
CREATE EXTENSION IF NOT EXISTS btree_gist;

-- Named challenge periods (weekly/monthly/seasonal competitions).
-- Auto-derived weekly/monthly leaderboards don't need a row here — that's done via date_trunc().
CREATE TABLE IF NOT EXISTS challenges (
  id          serial PRIMARY KEY,
  slug        citext UNIQUE NOT NULL,
  title       text   NOT NULL,
  description text,
  prize       text,
  starts_at   timestamptz NOT NULL,
  ends_at     timestamptz NOT NULL,
  created_at  timestamptz NOT NULL DEFAULT now(),
  CHECK (ends_at > starts_at),
  EXCLUDE USING gist (tstzrange(starts_at, ends_at) WITH &&)  -- no overlapping challenges
);
CREATE INDEX IF NOT EXISTS challenges_active ON challenges (starts_at, ends_at);

-- Score submissions.
CREATE TABLE IF NOT EXISTS scores (
  id           bigserial PRIMARY KEY,
  name         citext      NOT NULL CHECK (char_length(name) BETWEEN 1 AND 16),
  score        integer     NOT NULL CHECK (score >= 0 AND score <= 10000000),
  lines        integer     NOT NULL CHECK (lines >= 0 AND lines <= 100000),
  level        integer     NOT NULL CHECK (level BETWEEN 1 AND 100),
  duration_ms  integer     NOT NULL CHECK (duration_ms BETWEEN 1000 AND 86400000),
  client_id    uuid        NOT NULL,
  ip_hash      bytea       NOT NULL,                  -- sha256(ip + daily salt), never raw
  user_agent   text,
  country      text,                                  -- from x-vercel-ip-country
  created_at   timestamptz NOT NULL DEFAULT now(),
  challenge_id integer     REFERENCES challenges(id) ON DELETE SET NULL
);
CREATE INDEX IF NOT EXISTS scores_top_all      ON scores (score DESC, created_at DESC);
CREATE INDEX IF NOT EXISTS scores_by_created   ON scores (created_at DESC);
CREATE INDEX IF NOT EXISTS scores_by_challenge ON scores (challenge_id, score DESC) WHERE challenge_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS scores_ratelimit    ON scores (ip_hash, created_at DESC);
