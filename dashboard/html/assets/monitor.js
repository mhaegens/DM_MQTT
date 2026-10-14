(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const topics = new Map(), dirty = new Set(), pendingFeed = [];
  let client, renderTimer, received = 0, rate = 0, totalFeed = 0, measuring = false;
  const MAX_FEED = 100, MAX_TOPICS = 2000;
  const feed = $('feed-list'), tbody = $('topic-tbody');
  const ago = time => `${Math.floor((Date.now() - time) / 1000)}s ago`;
  const short = (s, n) => s.length > n ? s.slice(0, n) + '…' : s;
  const probeTopic = `sapdm/latency/${crypto.randomUUID ? crypto.randomUUID() : Math.random().toString(36).slice(2)}`;
  let probeToken, probeResolve, probeSent;
  $('broker-url').value = dmMqtt.url;

  function setConnected(yes) {
    $('status-badge').className = yes ? 'connected' : 'disconnected';
    $('status-label').textContent = yes ? 'Connected' : 'Disconnected';
    $('status-ts').textContent = yes ? new Date().toLocaleTimeString() : '';
    $('pub-btn').disabled = !yes;
    $('pub-btn').textContent = yes ? 'Publish' : 'Not connected';
    $('probe-btn').disabled = !yes || measuring;
    if (!yes) { $('device-state').textContent = 'Unknown'; $('device-rtt').textContent = '—'; }
  }

  function scheduleRender() {
    if (!renderTimer) renderTimer = setTimeout(flush, 16);
  }

  function record(topic, payload, outgoing = false) {
    if (topic === probeTopic) return;
    // Bound unknown topics as well as the feed; a wildcard stream can grow
    // indefinitely. Existing rows retain their DOM nodes during updates.
    if (!topics.has(topic) && topics.size >= MAX_TOPICS) {
      const oldest = topics.keys().next().value;
      topics.get(oldest).row?.remove(); topics.delete(oldest); dirty.delete(oldest);
    }
    const current = topics.get(topic) || {};
    current.value = payload; current.time = Date.now();
    topics.set(topic, current); dirty.add(topic);
    pendingFeed.push({topic, payload, outgoing, time: new Date().toLocaleTimeString()});
    if (pendingFeed.length > MAX_FEED) pendingFeed.shift();
    totalFeed++;
    scheduleRender();
  }

  function flush() {
    renderTimer = null;
    $('feed-empty')?.remove(); $('topic-empty')?.closest('tr').remove();
    const fragment = document.createDocumentFragment();
    for (const item of pendingFeed.splice(0).reverse()) {
      const row = document.createElement('div'); row.className = 'feed-row' + (item.outgoing ? ' feed-out' : '');
      const ts = document.createElement('div'); ts.className = 'feed-ts'; ts.textContent = item.time;
      const body = document.createElement('div'); body.className = 'feed-body';
      const topic = document.createElement('div'); topic.className = 'feed-topic'; topic.textContent = item.topic;
      const payload = document.createElement('div'); payload.className = 'feed-payload'; payload.textContent = short(item.payload, 120);
      if (item.outgoing) { const badge = document.createElement('span'); badge.className = 'feed-out-badge'; badge.textContent = 'OUT'; topic.append(badge); }
      body.append(topic, payload); row.append(ts, body); fragment.append(row);
    }
    feed.prepend(fragment);
    while (feed.children.length > MAX_FEED) feed.lastElementChild.remove();
    $('feed-count').textContent = `${totalFeed} messages · latest ${feed.children.length}`;
    let newRows = false;
    for (const topic of dirty) {
      const entry = topics.get(topic); if (!entry) continue;
      if (!entry.row) {
        entry.row = document.createElement('tr');
        for (const cls of ['td-topic', 'td-value', 'td-ago']) { const td = document.createElement('td'); td.className = cls; entry.row.append(td); }
        entry.row.children[0].textContent = topic; entry.row.children[0].title = topic;
        tbody.append(entry.row); newRows = true;
      }
      entry.row.children[1].textContent = short(entry.value, 60); entry.row.children[1].title = entry.value;
      entry.row.children[2].textContent = ago(entry.time);
    }
    // Sort only when the set of topics changes, never per telemetry message.
    if (newRows) {
      const ordered = document.createDocumentFragment();
      for (const [, value] of [...topics].sort(([a], [b]) => a.localeCompare(b))) ordered.append(value.row);
      tbody.append(ordered);
    }
    dirty.clear(); $('topic-count').textContent = `${topics.size} topics`;
    $('received-count').textContent = received.toLocaleString();
  }

  function connect() {
    let url;
    try { url = $('broker-url').value.trim(); dmMqtt.save(url); }
    catch (err) { $('probe-result').textContent = err.message; return; }
    if (client) { client.removeAllListeners(); client.end(true); }
    setConnected(false);
    client = mqtt.connect(url, dmMqtt.options);
    client.on('connect', () => {
      client.subscribe('#', {qos: 0}, err => {
        setConnected(!err);
        if (!err) client.publish("factory/device/request_snapshot", "state", {qos: 0, retain: false});
        if (err) $('probe-result').textContent = `Subscription failed: ${err.message}`;
      });
    });
    for (const event of ['reconnect', 'close', 'offline', 'disconnect']) client.on(event, () => setConnected(false));
    client.on('error', err => { setConnected(false); $('probe-result').textContent = err.message; });
    client.on('message', (topic, message) => {
      const value = message.toString();
      if (topic === probeTopic && value === probeToken && probeResolve) {
        const done = probeResolve; probeResolve = null; done(performance.now() - probeSent); return;
      }
      if (topic === probeTopic) return;
      received++; rate++;
      if (topic === 'factory/device/status') $('device-state').textContent = value === 'online' ? 'Online' : 'Offline';
      if (topic === 'factory/device/performance') {
        try { const stats = JSON.parse(value); $('device-rtt').textContent = stats.rtt_ms >= 0 ? `${stats.rtt_ms} ms` : '—'; } catch (_) {}
      }
      record(topic, value);
    });
  }

  setInterval(() => {
    $('message-rate').textContent = `${rate} /s`; rate = 0;
    if (!document.hidden) for (const entry of topics.values()) if (entry.row) entry.row.children[2].textContent = ago(entry.time);
  }, 1000);
  $('connect-btn').addEventListener('click', connect);
  $('pub-btn').addEventListener('click', () => {
    const topic = $('pub-topic').value.trim(), value = $('pub-payload').value;
    if (!topic || /[+#\0]/.test(topic)) { $('pub-topic').focus(); $('probe-result').textContent = 'Publish topics cannot contain + or # wildcards.'; return; }
    if (!client?.connected) return;
    client.publish(topic, value, {qos: Number($('pub-qos').value)}, err => {
      if (err) $('probe-result').textContent = `Publish failed: ${err.message}`;
      else record(topic, value, true);
    });
  });
  $('probe-btn').addEventListener('click', async () => {
    if (!client?.connected || measuring) return;
    measuring = true; $('probe-btn').disabled = true;
    const samples = []; let failed = 0;
    for (let i = 0; i < 20; i++) {
      if (!client.connected) { failed += 20 - i; break; }
      $('probe-result').textContent = `Measuring ${i + 1} / 20…`;
      const ms = await new Promise(resolve => {
        const timeout = setTimeout(() => { probeResolve = null; resolve(null); }, 2000);
        probeResolve = result => { clearTimeout(timeout); resolve(result); };
        probeToken = `${i}-${Math.random()}`; probeSent = performance.now();
        client.publish(probeTopic, probeToken, {qos: 0}, err => { if (err && probeResolve) { const done = probeResolve; probeResolve = null; done(null); } });
      });
      if (ms === null) failed++; else samples.push(ms);
      if (i < 19) await new Promise(resolve => setTimeout(resolve, 100));
    }
    samples.sort((a, b) => a - b);
    const percentile = p => samples[Math.max(0, Math.ceil(samples.length * p) - 1)].toFixed(1);
    $('probe-result').textContent = samples.length ? `Browser ↔ broker: p50 ${percentile(.5)} ms · p95 ${percentile(.95)} ms · ${failed} timed out` : 'No round-trip replies received.';
    measuring = false; $('probe-btn').disabled = !client.connected;
  });
  if (typeof mqtt === 'undefined') $('probe-result').textContent = 'MQTT library could not load. Check server assets.';
  else connect();
})();
