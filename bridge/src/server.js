'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const yaml = require('js-yaml');
const { isConnected } = require('./mqtt-client');
const { reloadMapping } = require('./mapper');
const { getLastDmPost, getPendingQueue } = require('./stats');

const CONFIG_PATH = path.join(__dirname, '../config/mapping.yaml');
const PORT = 3001;
const startTime = Date.now();

const server = http.createServer((req, res) => {
  const { method, url } = req;

  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, PUT, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

  if (method === 'OPTIONS') {
    res.writeHead(204); res.end(); return;
  }

  if (method === 'GET' && url === '/healthz') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      status: isConnected() ? 'ok' : 'degraded',
      broker: isConnected() ? 'connected' : 'disconnected',
      lastDmPost: getLastDmPost(),
      pendingQueue: getPendingQueue(),
      configLoaded: fs.existsSync(CONFIG_PATH),
      uptimeSeconds: Math.floor((Date.now() - startTime) / 1000),
    }));
    return;
  }

  if (method === 'GET' && url === '/api/config') {
    try {
      const raw = fs.readFileSync(CONFIG_PATH, 'utf8');
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(yaml.load(raw)));
    } catch (err) {
      if (err.code === 'ENOENT') {
        res.writeHead(404, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'No mapping config yet — save one from the editor Bridge tab' }));
      } else {
        res.writeHead(500, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: err.message }));
      }
    }
    return;
  }

  if (method === 'PUT' && url === '/api/config') {
    let body = '';
    req.on('data', chunk => { body += chunk; });
    req.on('end', () => {
      try {
        const obj = JSON.parse(body);
        if (!obj || typeof obj !== 'object' || !obj.machines) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ error: 'Invalid config: missing machines key' }));
          return;
        }
        fs.mkdirSync(path.dirname(CONFIG_PATH), { recursive: true });
        fs.writeFileSync(CONFIG_PATH, yaml.dump(obj, { lineWidth: 120 }));
        reloadMapping();
        log({ event: 'config_updated_via_api' });
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      } catch (err) {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: err.message }));
      }
    });
    return;
  }

  res.writeHead(404); res.end();
});

function startServer() {
  server.listen(PORT, () => {
    log({ event: 'http_server_started', port: PORT });
  });
}

function log(obj) {
  console.log(JSON.stringify({ ts: new Date().toISOString(), src: 'server', ...obj }));
}

module.exports = { startServer };
