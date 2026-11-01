'use strict';
const { getToken, invalidateToken } = require('./auth');
const { setLastDmPost } = require('./stats');

const DM_API_HOST = process.env.DM_API_HOST;

// Core fetch with retry + token refresh on 401
async function dmFetch(method, urlPath, body, retries = 3) {
  if (!DM_API_HOST) {
    log({ event: 'dm_skip', reason: 'DM_API_HOST not configured' });
    return null;
  }
  for (let attempt = 0; attempt <= retries; attempt++) {
    const t0 = Date.now();
    try {
      const token = await getToken();
      const res = await fetch(`https://${DM_API_HOST}${urlPath}`, {
        method,
        headers: {
          Authorization: `Bearer ${token}`,
          'Content-Type': 'application/json',
        },
        body: body != null ? JSON.stringify(body) : undefined,
      });
      const durationMs = Date.now() - t0;

      if (res.status === 401) {
        invalidateToken();
        if (attempt < retries) { await sleep(1000); continue; }
      }

      if (res.ok || res.status === 202 || res.status === 201) {
        log({ event: 'dm_api', method, path: urlPath, status: res.status, durationMs });
        setLastDmPost();
        return res;
      }

      if (res.status >= 500 && attempt < retries) {
        const delay = 2000 * Math.pow(2, attempt);
        log({ event: 'dm_retry', method, path: urlPath, status: res.status, attempt, delay });
        await sleep(delay);
        continue;
      }

      const text = await res.text().catch(() => '');
      log({ event: 'dm_api_error', method, path: urlPath, status: res.status, body: text.slice(0, 300), durationMs });
      throw new Error(`DM ${res.status}: ${text.slice(0, 200)}`);
    } catch (err) {
      if (attempt === retries) {
        log({ event: 'dm_failed', method, path: urlPath, error: err.message });
        throw err;
      }
      await sleep(2000 * Math.pow(2, attempt));
    }
  }
}

// ─── M2: Last Indicator Value ────────────────────────────────────────────────

async function postIndicatorValue(plant, workCenter, assetId, indicator, value, unitOfMeasure) {
  await dmFetch('POST', '/linemonitor/v1/lastindicator/value', {
    plant,
    workCenter,
    assetId,
    indicator,
    value: String(value),
    unitOfMeasure,
    dateTime: new Date().toISOString(),
  });
}

// ─── M3: SFC Execution ───────────────────────────────────────────────────────

async function startSfc(plant, operation, resource, sfcs) {
  await dmFetch('POST', '/sfc/v1/sfcs/start', { plant, operation, resource, sfcs });
}

async function signoffSfc(plant, operation, resource, sfcs) {
  await dmFetch('POST', '/sfc/v1/sfcs/signoff', { plant, operation, resource, sfcs });
}

async function getSfcsInWork(plant, resource) {
  const res = await dmFetch('GET', `/sfc/v1/sfcsInWork?plant=${encodeURIComponent(plant)}&resource=${encodeURIComponent(resource)}`);
  if (!res) return [];
  return res.json().catch(() => []);
}

async function getSfcWorklist(plant, resource, sfcStatuses = 'IN_QUEUE') {
  const res = await dmFetch('GET', `/sfc/v1/worklist/sfcs?plant=${encodeURIComponent(plant)}&resource=${encodeURIComponent(resource)}&sfcStatuses=${sfcStatuses}`);
  if (!res) return [];
  return res.json().catch(() => []);
}

// ─── M3: Time Tracking ───────────────────────────────────────────────────────

async function startResourceUsage(plant, resource, userId) {
  await dmFetch('POST', '/v1/resource-usage/start', { plant, resource, userId, activity: 'PRODUCTION' });
}

async function stopResourceUsage(plant, resource, userId) {
  await dmFetch('POST', '/v1/resource-usage/stop', { plant, resource, userId, activity: 'PRODUCTION' });
}

// ─── M3: OEE Downtime ────────────────────────────────────────────────────────

async function recordDowntime(plant, resource, status, startDateTime, reasonCode1) {
  await dmFetch('POST', '/oee/v1/downtime', [{
    plant,
    resource,
    status,
    startDateTime,
    reasonCode1,
  }]);
}

// ─── M3: Quantity Confirmation ───────────────────────────────────────────────

async function confirmQuantity(plant, shopOrder, sfc, operationActivity, workCenter, yieldQuantity, yieldQuantityUnit) {
  await dmFetch('POST', '/quantityConfirmation/v1/confirm', {
    plant,
    shopOrder,
    sfc,
    operationActivity,
    workCenter,
    yieldQuantity,
    yieldQuantityUnit,
    postingDateTime: new Date().toISOString(),
  });
}

// ─── M4: Data Collection ─────────────────────────────────────────────────────

async function logDataCollection(plant, resource, sfcs, dcGroup, groupVersion, operation, parameterValues) {
  await dmFetch('POST', '/datacollection/v1/log', {
    plant,
    resource,
    sfcs,
    group: { dcGroup, version: groupVersion },
    operation: { operation, version: '1' },
    parameterValues,
    postingDateTime: new Date().toISOString(),
  });
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

function log(obj) {
  console.log(JSON.stringify({ ts: new Date().toISOString(), src: 'dm', ...obj }));
}

module.exports = {
  postIndicatorValue,
  startSfc, signoffSfc, getSfcsInWork, getSfcWorklist,
  startResourceUsage, stopResourceUsage,
  recordDowntime,
  confirmQuantity,
  logDataCollection,
};
