'use strict';
const mqtt = require('mqtt');
const { handleSignal } = require('./mapper');

const BROKER_URL = process.env.MQTT_BROKER_URL || 'mqtt://mosquitto:1883';
const RECONNECT_DELAYS = [5000, 10000, 30000];

let _connected = false;
let _reconnectAttempts = 0;
let _client = null;

function isConnected() { return _connected; }

function parseTopic(topic, payloadStr) {
  const parts = topic.split('/');

  // factory/machines/{id}/status|pulse|{metric}
  if (parts[0] === 'factory' && parts[1] === 'machines' && parts.length === 4) {
    const [, , machineId, signal] = parts;
    const type = signal === 'status' ? 'status' : signal === 'pulse' ? 'pulse' : 'metric';
    return { type, machineId, metric: signal, value: payloadStr.trim() };
  }

  // factory/tanks/{id}/{metric}
  if (parts[0] === 'factory' && parts[1] === 'tanks' && parts.length === 4) {
    const [, , tankId, metric] = parts;
    return { type: 'tank_metric', tankId, metric, value: payloadStr.trim() };
  }

  // factory/operators/login (JSON payload)
  if (topic === 'factory/operators/login') {
    try {
      return { type: 'operator_login', ...JSON.parse(payloadStr) };
    } catch {
      return null;
    }
  }

  return null;
}

function connect() {
  log({ event: 'mqtt_connecting', url: BROKER_URL });
  _client = mqtt.connect(BROKER_URL, {
    reconnectPeriod: 0, // manual backoff so we control the delays
    keepalive: 60,
    will: {
      topic: 'factory/bridge/status',
      payload: 'offline',
      retain: true,
      qos: 0,
    },
  });

  _client.on('connect', () => {
    _connected = true;
    _reconnectAttempts = 0;
    log({ event: 'mqtt_connected' });
    _client.publish('factory/bridge/status', 'online', { retain: true, qos: 0 });
    _client.subscribe('factory/#', { qos: 0 });
  });

  _client.on('message', (topic, payload) => {
    const signal = parseTopic(topic, payload.toString());
    if (signal) {
      handleSignal(signal).catch(err =>
        log({ event: 'signal_error', topic, error: err.message })
      );
    }
  });

  _client.on('close', () => {
    _connected = false;
    const delay = RECONNECT_DELAYS[Math.min(_reconnectAttempts, RECONNECT_DELAYS.length - 1)];
    _reconnectAttempts++;
    log({ event: 'mqtt_disconnected', reconnect_in_ms: delay });
    setTimeout(connect, delay);
  });

  _client.on('error', (err) => {
    log({ event: 'mqtt_error', error: err.message });
  });
}

function startMqttClient() {
  connect();
}

function log(obj) {
  console.log(JSON.stringify({ ts: new Date().toISOString(), src: 'mqtt', ...obj }));
}

module.exports = { startMqttClient, isConnected };
