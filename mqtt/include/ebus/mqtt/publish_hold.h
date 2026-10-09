#pragma once
// Publishes made while the broker link is down, kept for the next connect. The rules are
// ebus-mqtt-client's (README "Publishing before the connection is up"):
//
//   - retained, any QoS: held, newest value per topic. A newer value for a held topic
//     replaces the older one and moves to the back of the flush order.
//   - not retained, QoS 1 or 2: held in order, one entry per publish.
//   - not retained, QoS 0: dropped.
//
// Bounded: when full, the oldest entry is evicted. Fixed storage in the object, no
// allocation; declare it static, not on a stack. Not thread-safe: only the task that owns
// the MQTT client touches it.
//
// The sizes below set the object's layout, so a -D that overrides one must reach every
// translation unit that includes this header (PlatformIO build_flags, or a PUBLIC
// compile definition on the ebus_mqtt CMake target), not only this library's sources.
#include <stdint.h>

#ifndef EBUS_MQTT_HOLD_ENTRIES
#define EBUS_MQTT_HOLD_ENTRIES 64
#endif
// Longest topic held, in characters. A Homie topic is at most HOMIE_TOPIC_MAX (127).
#ifndef EBUS_MQTT_HOLD_TOPIC_MAX
#define EBUS_MQTT_HOLD_TOPIC_MAX 127
#endif
// Longest payload held, in bytes. A longer one (a Homie $description) is dropped.
#ifndef EBUS_MQTT_HOLD_PAYLOAD_MAX
#define EBUS_MQTT_HOLD_PAYLOAD_MAX 1024
#endif

class PublishHold {
 public:
    enum Result {
        HELD,
        REPLACED,          // a held retained value for the same topic was replaced
        EVICTED_OLDEST,    // held, after evicting the oldest entry to make room
        DROPPED_QOS0,
        DROPPED_TOO_LARGE  // topic or payload over the limits above
    };

    // Sends one held publish on a live link. Returns true when the entry is done with
    // (sent, or refused for good), false to keep it held and stop the flush.
    typedef bool (*send_fn)(void* ctx, const char* topic, const char* payload, int length,
                            bool retained, int qos);

    Result hold(const char* topic, const char* payload, int length, bool retained, int qos);

    // Send every held publish, oldest first, removing each one sent. Stops at the first
    // that fails and keeps it and everything after it. Returns the number sent.
    int flush(send_fn send, void* ctx);

    void clear();
    int count() const { return _count; }
    // Entries evicted because the hold was full, since construction.
    uint32_t evicted() const { return _evicted; }

 private:
    struct Entry {
        bool used = false;
        bool retained = false;
        int qos = 0;
        int length = 0;
        uint32_t seq = 0;
        char topic[EBUS_MQTT_HOLD_TOPIC_MAX + 1] = {0};
        char payload[EBUS_MQTT_HOLD_PAYLOAD_MAX] = {0};
    };

    int oldest() const;

    Entry _entries[EBUS_MQTT_HOLD_ENTRIES];
    int _count = 0;
    uint32_t _next_seq = 0;
    uint32_t _evicted = 0;
};
