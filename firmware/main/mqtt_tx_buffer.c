#include "mqtt_tx_buffer.h"
#include <stdio.h>
#include <string.h>

bool mqtt_tx_push(MqttTxBuffer *b, const MqttTxMessage *m, bool metric)
{
    if (!memchr(m->topic, 0, sizeof(m->topic)) || !m->topic[0] ||
        !memchr(m->payload, 0, sizeof(m->payload))) {
        b->rejected++;
        return false;
    }
    if (!metric) {
        if (b->event_count == MQTT_TX_EVENTS) { b->rejected++; return false; }
        unsigned slot = (b->event_head + b->event_count++) % MQTT_TX_EVENTS;
        b->events[slot] = *m;
        b->events[slot].is_metric = false;
        return true;
    }
    if (strlen(m->payload) >= sizeof(b->metrics[0].payload)) {
        b->rejected++;
        return false;
    }
    int slot = -1;
    for (unsigned i = 0; i < MQTT_TX_METRICS; i++) {
        if (!strcmp(b->metrics[i].topic, m->topic)) { slot = (int)i; break; }
        if (!b->metrics[i].topic[0] && slot < 0) slot = (int)i;
    }
    if (slot < 0) { b->rejected++; return false; }
    MqttTxMetric *v = &b->metrics[slot];
    // Compare the actual two-decimal wire payload. Repeated slider callbacks,
    // sim snapshots and sub-cent changes do not create network traffic.
    if ((v->pending && !strcmp(v->payload, m->payload)) ||
        (!v->pending && v->inflight && !strcmp(v->inflight_payload, m->payload)) ||
        (!v->pending && !v->inflight && v->published_valid && !strcmp(v->published_payload, m->payload))) {
        b->suppressed++;
        return true;
    }
    if (v->pending) b->coalesced++;
    // A -> B -> A before B was sent: cancel B instead of retransmitting A.
    if (!v->inflight && v->published_valid && !strcmp(v->published_payload, m->payload)) {
        v->pending = false; b->suppressed++; return true;
    }
    strcpy(v->topic, m->topic);
    strcpy(v->payload, m->payload);
    v->created_ms = m->created_ms;
    v->pending = true;
    return true;
}

bool mqtt_tx_pop(MqttTxBuffer *b, MqttTxMessage *m)
{
    if (b->event_count) {
        *m = b->events[b->event_head];
        b->event_head = (b->event_head + 1) % MQTT_TX_EVENTS;
        b->event_count--;
        return true;
    }
    for (unsigned n = 0; n < MQTT_TX_METRICS; n++) {
        unsigned i = (b->next_metric + n) % MQTT_TX_METRICS;
        MqttTxMetric *v = &b->metrics[i];
        if (!v->pending) continue;
        memset(m, 0, sizeof(*m));
        strcpy(m->topic, v->topic);
        strcpy(m->payload, v->payload);
        m->created_ms = v->created_ms;
        m->is_metric = true;
        v->pending = false;
        v->inflight = true;
        strcpy(v->inflight_payload, v->payload);
        b->next_metric = (i + 1) % MQTT_TX_METRICS;
        return true;
    }
    return false;
}

void mqtt_tx_finish(MqttTxBuffer *b, const MqttTxMessage *m, bool success)
{
    if (!m->is_metric) return;
    for (unsigned i = 0; i < MQTT_TX_METRICS; i++) {
        MqttTxMetric *v = &b->metrics[i];
        if (strcmp(v->topic, m->topic)) continue;
        v->inflight = false;
        if (success) { strcpy(v->published_payload, m->payload); v->published_valid = true; }
        else v->published_valid = false;
        return;
    }
}

void mqtt_tx_clear_metrics(MqttTxBuffer *b)
{
    for (unsigned i = 0; i < MQTT_TX_METRICS; i++) {
        b->metrics[i].pending = false; b->metrics[i].inflight = false; b->metrics[i].published_valid = false;
    }
}

unsigned mqtt_tx_pending(const MqttTxBuffer *b)
{
    unsigned n = b->event_count;
    for (unsigned i = 0; i < MQTT_TX_METRICS; i++) n += b->metrics[i].pending;
    return n;
}
