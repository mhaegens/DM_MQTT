'use strict';

// Shared runtime counters used by both dm-client.js and server.js.
// A separate module avoids circular requires.

let _lastDmPost = null;
let _pendingQueue = 0;

function setLastDmPost() { _lastDmPost = new Date().toISOString(); }
function getLastDmPost() { return _lastDmPost; }
function setPendingQueue(n) { _pendingQueue = n; }
function getPendingQueue() { return _pendingQueue; }

module.exports = { setLastDmPost, getLastDmPost, setPendingQueue, getPendingQueue };
