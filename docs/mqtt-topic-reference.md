# MQTT Topic Reference

All telemetry published by the firmware uses the prefix `factory` (defined as `TOPIC_PREFIX` in `config.h`). Topics follow the pattern `factory/{entity_type}/{entity_id}/{metric_key}`.

---

## Topic Namespace

```
factory/
  machines/
    {id}/status       Running state
    {id}/pulses       Production pulse counter
    {id}/{metric_key} Any metric value
  tanks/
    {id}/{metric_key} Any metric value
  operators/
    login             Operator badge activation event
```

---

## Machine Topics

| Topic | Payload | QoS | Retain | Published |
|-------|---------|-----|--------|-----------|
| `factory/machines/{id}/status` | `RUNNING` or `STOPPED` (string) | 0 | false | Every sim tick (~1.5 s) |
| `factory/machines/{id}/pulses` | Integer string, e.g. `348` | 0 | false | Every 5 ticks (~7.5 s), running machines only |
| `factory/machines/{id}/{metric_key}` | Float string, 2 decimal places, e.g. `4850.00` | 0 | false | Every sim tick (~1.5 s) |

The `{id}` field matches the `id` value in the JSON config file. Common metric keys in the default config: `rpm`, `spindleLoad`, `toolWear`, `feedRate`, `machTemp`, `flowRate`, `fillPres`, `lineSpeed`, `oee`, `current`, `voltage`, `wireSpeed`, `cycleTime`, `torque`, `thickness`, `speed`.

---

## Tank Topics

| Topic | Payload | QoS | Retain | Published |
|-------|---------|-----|--------|-----------|
| `factory/tanks/{id}/{metric_key}` | Float string, 2 decimal places | 0 | false | Every sim tick (~1.5 s) |

Common metric keys in the default config: `level`, `temperature`, `flowIn`, `pressure`, `ph`, `flowOut`.

---

## Operator Login Topic

| Topic | Payload | QoS | Retain | Published |
|-------|---------|-----|--------|-----------|
| `factory/operators/login` | JSON object (see below) | 0 | false | On badge selection in the People tab |

Payload format:

```json
{
  "uid": "A3:F7:C2:91",
  "name": "Demo Operator",
  "role": "Shift Lead",
  "plant": "Plant BE01",
  "ts": 12345678
}
```

`ts` is milliseconds since boot (from `esp_timer_get_time() / 1000`), not a wall-clock timestamp.

The operator login message is the only JSON payload in this system. All other payloads are plain strings.

---

## Default Entity IDs

These are the IDs used in the production config at `firmware/main/spiffs_image/sapdm_config.json`.

### Machines

| ID | Name | Type |
|----|------|------|
| `cnc01` | CNC-01 | CNC Machining |
| `cnc02` | CNC-02 | CNC Machining |
| `fill01` | FILL-01 | Filling Line |
| `pack01` | PACK-01 | Packaging |
| `weld01` | WELD-01 | Welding |
| `asm01` | ASM-01 | Assembly |
| `coat01` | COAT-01 | Coating |

### Tanks

| ID | Name |
|----|------|
| `raw` | Raw Material |
| `prod` | Production |
| `waste` | Waste |

These IDs are config-driven. A different `sapdm_config.json` can use different IDs.

---

## Payload Format Notes

- All numeric values are plain float strings formatted as `%.2f`, for example `4850.00` or `68.00`. They are not wrapped in JSON.
- Status values are the literal strings `RUNNING` or `STOPPED`.
- Pulse counts are plain integer strings, for example `348`.
- QoS is 0 (fire-and-forget). No retain flags are set.
- The operator login is the only JSON-encoded payload.

---

## SAP DM Integration

SAP Digital Manufacturing IoT ingests data via MQTT subscribe. Connect to:

```
wss://mqtt.example.com
```

Subscribe to `factory/#` to receive all telemetry. The broker uses no authentication (`allow_anonymous true` in `mosquitto.conf`). This is intentional for demo simplicity. For a production deployment, add `password_file` authentication to `mosquitto.conf` and update the firmware `MQTT_BROKER_URI` with credentials.

---

## Testing

Subscribe to all topics from the broker host or LAN:

```bash
mosquitto_sub \
  -h localhost \
  -p 9001 \
  --protocol-version mqttv311 \
  -t 'factory/#' \
  -v
```

Or from outside the server using the public endpoint (requires WebSocket support; `mosquitto_sub` v2.0+ supports `--ws`):

```bash
mosquitto_sub \
  -h mqtt.example.com \
  -p 443 \
  --capath /etc/ssl/certs \
  --protocol-version mqttv311 \
  --ws \
  -t 'factory/#' \
  -v
```
