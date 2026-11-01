'use strict';
require('dotenv').config();
const { loadMapping } = require('./src/mapper');
const { startMqttClient } = require('./src/mqtt-client');
const { startServer } = require('./src/server');

loadMapping();
startMqttClient();
startServer();
