#include "mqtt_tx_buffer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MqttTxMessage message(const char *topic, const char *payload) {
    MqttTxMessage m = {0};
    snprintf(m.topic,sizeof(m.topic),"%s",topic); snprintf(m.payload,sizeof(m.payload),"%s",payload);
    return m;
}
int main(void) {
    MqttTxBuffer *b = calloc(1,sizeof(*b)); assert(b);
    MqttTxMessage m = message("factory/tanks/raw/level","1"), result;
    for (int i=0;i<10000;i++) { snprintf(m.payload,sizeof(m.payload),"%d",i); assert(mqtt_tx_push(b,&m,true)); }
    assert(mqtt_tx_pending(b)==1 && b->coalesced==9999);
    MqttTxMessage event = message("factory/operators/login","operator 1"); event.qos=1;
    assert(mqtt_tx_push(b,&event,false));
    assert(mqtt_tx_pop(b,&result) && !strcmp(result.payload,"operator 1") && result.qos==1);
    assert(mqtt_tx_pop(b,&result) && !strcmp(result.payload,"9999"));
    assert(!mqtt_tx_pop(b,&result));
    for (int i=0;i<MQTT_TX_EVENTS;i++) { snprintf(event.payload,sizeof(event.payload),"event %d",i); assert(mqtt_tx_push(b,&event,false)); }
    assert(!mqtt_tx_push(b,&event,false) && b->rejected==1);
    for (int i=0;i<MQTT_TX_EVENTS;i++) { assert(mqtt_tx_pop(b,&result)); char expected[24]; snprintf(expected,sizeof(expected),"event %d",i); assert(!strcmp(result.payload,expected)); }
    for (int i=1;i<MQTT_TX_METRICS;i++) { snprintf(m.topic,sizeof(m.topic),"factory/metric/%d",i); assert(mqtt_tx_push(b,&m,true)); }
    assert(mqtt_tx_pending(b)==MQTT_TX_METRICS-1);
    mqtt_tx_clear_metrics(b); assert(mqtt_tx_pending(b)==0);
    m=message("factory/tanks/raw/level","latest after reconnect"); assert(mqtt_tx_push(b,&m,true));
    assert(mqtt_tx_pop(b,&result) && !strcmp(result.payload,"latest after reconnect"));
    memset(m.topic,'x',sizeof(m.topic)); assert(!mqtt_tx_push(b,&m,true));
    memset(b,0,sizeof(*b));
    m=message("factory/tanks/raw/level","1.00"); assert(mqtt_tx_push(b,&m,true));
    assert(mqtt_tx_pop(b,&result)); mqtt_tx_finish(b,&result,true);
    for(int i=0;i<10000;i++) assert(mqtt_tx_push(b,&m,true));
    assert(mqtt_tx_pending(b)==0 && b->suppressed==10000);
    m=message("factory/tanks/raw/level","2.00"); assert(mqtt_tx_push(b,&m,true));
    assert(mqtt_tx_pop(b,&result)); // B is in flight while the user returns to A
    m=message("factory/tanks/raw/level","1.00"); assert(mqtt_tx_push(b,&m,true));
    mqtt_tx_finish(b,&result,true); assert(mqtt_tx_pending(b)==1);
    assert(mqtt_tx_pop(b,&result) && !strcmp(result.payload,"1.00")); mqtt_tx_finish(b,&result,true);
    m=message("factory/tanks/raw/level","2.00"); assert(mqtt_tx_push(b,&m,true));
    m=message("factory/tanks/raw/level","1.00"); assert(mqtt_tx_push(b,&m,true));
    assert(mqtt_tx_pending(b)==0); // unsent B was canceled
    mqtt_tx_clear_metrics(b); assert(mqtt_tx_push(b,&m,true));
    assert(mqtt_tx_pending(b)==1); // reconnect must send a fresh snapshot
    assert(mqtt_tx_pop(b,&result)); mqtt_tx_finish(b,&result,false);
    assert(mqtt_tx_push(b,&m,true) && mqtt_tx_pending(b)==1); // failed send must not poison the cache
    free(b); puts("MQTT buffer: burst coalescing, event priority/FIFO, saturation, reconnect snapshot, string bounds, unchanged suppression, in-flight reversal, reconnect and failed-send cache reset passed");
}
