'use strict';

// Interval-based throttle: collects submitted values and flushes dirty entries every intervalMs.
// Also supports immediate flush for all keys matching a prefix (used on machine STOPPED).
class ThrottleMap {
  constructor(intervalMs = 5000) {
    this._interval = intervalMs;
    this._map = new Map(); // key -> { value, dirty }
    this._onFlush = null;
    this._timer = setInterval(() => this._tick(), intervalMs);
    this._timer.unref(); // don't keep process alive if only this timer is left
  }

  setFlushCallback(fn) {
    this._onFlush = fn;
  }

  // Queue a value for the given key; it will be sent on the next tick if dirty.
  submit(key, value) {
    this._map.set(key, { value, dirty: true });
  }

  // Immediately flush all dirty keys matching the given prefix.
  flushPrefix(prefix) {
    for (const [key, entry] of this._map) {
      if (key.startsWith(prefix) && entry.dirty && this._onFlush) {
        this._onFlush(key, entry.value).catch(err =>
          console.error(JSON.stringify({ ts: new Date().toISOString(), src: 'throttle', event: 'flush_error', key, error: err.message }))
        );
        this._map.set(key, { value: entry.value, dirty: false });
      }
    }
  }

  _tick() {
    for (const [key, entry] of this._map) {
      if (entry.dirty && this._onFlush) {
        this._onFlush(key, entry.value).catch(err =>
          console.error(JSON.stringify({ ts: new Date().toISOString(), src: 'throttle', event: 'flush_error', key, error: err.message }))
        );
        this._map.set(key, { value: entry.value, dirty: false });
      }
    }
  }
}

module.exports = { ThrottleMap };
