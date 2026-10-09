// SPDX-License-Identifier: MIT
#include <ebus/mqtt/publish_hold.h>
#include <string.h>

int PublishHold::oldest() const {
    int best = -1;
    for (int i = 0; i < EBUS_MQTT_HOLD_ENTRIES; i++) {
        if (!_entries[i].used) continue;
        // Sequence numbers are compared as a difference, so a wrap keeps the order.
        if (best < 0 || (int32_t)(_entries[i].seq - _entries[best].seq) < 0) best = i;
    }
    return best;
}

PublishHold::Result PublishHold::hold(const char* topic, const char* payload, int length,
                                      bool retained, int qos) {
    if (!retained && qos == 0) return DROPPED_QOS0;
    if (length < 0) length = 0;
    size_t topic_len = strlen(topic);
    if (length > EBUS_MQTT_HOLD_PAYLOAD_MAX || topic_len > EBUS_MQTT_HOLD_TOPIC_MAX) {
        return DROPPED_TOO_LARGE;
    }

    int slot = -1;
    Result result = HELD;
    if (retained) {
        for (int i = 0; i < EBUS_MQTT_HOLD_ENTRIES; i++) {
            if (_entries[i].used && _entries[i].retained &&
                strcmp(_entries[i].topic, topic) == 0) {
                slot = i;
                result = REPLACED;
                break;
            }
        }
    }
    if (slot < 0) {
        for (int i = 0; i < EBUS_MQTT_HOLD_ENTRIES; i++) {
            if (!_entries[i].used) { slot = i; break; }
        }
    }
    if (slot < 0) {
        slot = oldest();
        _evicted++;
        _entries[slot].used = false;
        _count--;
        result = EVICTED_OLDEST;
    }

    Entry& e = _entries[slot];
    if (!e.used) _count++;
    e.used = true;
    e.retained = retained;
    e.qos = qos;
    e.length = length;
    e.seq = _next_seq++;
    memcpy(e.topic, topic, topic_len + 1);
    if (length > 0) memcpy(e.payload, payload, (size_t)length);
    return result;
}

int PublishHold::flush(send_fn send, void* ctx) {
    int sent = 0;
    for (int i = oldest(); i >= 0; i = oldest()) {
        Entry& e = _entries[i];
        if (!send(ctx, e.topic, e.payload, e.length, e.retained, e.qos)) break;
        e.used = false;
        _count--;
        sent++;
    }
    return sent;
}

void PublishHold::clear() {
    for (int i = 0; i < EBUS_MQTT_HOLD_ENTRIES; i++) _entries[i].used = false;
    _count = 0;
}
