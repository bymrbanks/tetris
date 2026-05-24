-- One example active challenge for testing the banner + challenge leaderboard.
-- Edit dates / prize as you like.

INSERT INTO challenges (slug, title, description, prize, starts_at, ends_at)
VALUES (
  'launch-week',
  'Launch Week',
  'First competition — fight for the crown.',
  '$20 USD',
  now() - interval '1 day',
  now() + interval '7 days'
)
ON CONFLICT (slug) DO UPDATE SET prize = EXCLUDED.prize;
