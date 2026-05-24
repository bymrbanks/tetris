// Admin auth — single shared bearer-style token in x-admin-token header.
// Token lives in ADMIN_TOKEN env var (set via `vercel env add ADMIN_TOKEN production`).

export function isAdmin(req) {
  const got = req.headers['x-admin-token'];
  const want = process.env.ADMIN_TOKEN;
  return !!want && typeof got === 'string' && got === want;
}
