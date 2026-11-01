'use strict';

const { DM_CLIENT_ID, DM_CLIENT_SECRET, DM_TOKEN_URL } = process.env;

let _cache = null;

async function getToken() {
  if (_cache && Date.now() < _cache.expires_at - 60_000) {
    return _cache.access_token;
  }
  if (!DM_CLIENT_ID || !DM_TOKEN_URL) {
    throw new Error('DM_CLIENT_ID / DM_TOKEN_URL not configured — set them in bridge/.env');
  }
  const body = new URLSearchParams({
    grant_type: 'client_credentials',
    client_id: DM_CLIENT_ID,
    client_secret: DM_CLIENT_SECRET,
  });
  const res = await fetch(DM_TOKEN_URL, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body,
  });
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`Token fetch ${res.status}: ${text.slice(0, 200)}`);
  }
  const data = await res.json();
  _cache = {
    access_token: data.access_token,
    expires_at: Date.now() + data.expires_in * 1000,
  };
  log({ event: 'token_fetched', expires_in: data.expires_in });
  return _cache.access_token;
}

function invalidateToken() {
  _cache = null;
  log({ event: 'token_invalidated' });
}

function log(obj) {
  console.log(JSON.stringify({ ts: new Date().toISOString(), src: 'auth', ...obj }));
}

module.exports = { getToken, invalidateToken };
