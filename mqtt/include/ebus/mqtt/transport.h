#pragma once
// The MQTT client as a library sees it: publish, a publish that is safe from any task,
// subscribe. A port implements it over its own MQTT client.
#include <string.h>

// Reports what became of a queue_publish(): called exactly once per call, with the
// payload that was queued, when the message is sent (sent=true) or dropped.
typedef void (*mqtt_publish_done_fn)(void* ctx, const char* payload, int length, bool sent);

class MqttTransport {
 public:
    // Publish exactly `length` bytes (a zero-length retained payload deletes the topic).
    // Only on the task that owns the client. Returns false when the client is not
    // connected or the publish failed.
    virtual bool publish(const char* topic, const char* payload, int length, bool retained,
                         int qos) = 0;
    // NUL-terminated payload.
    bool publish(const char* topic, const char* payload, bool retained, int qos) {
        return publish(topic, payload, (int)strlen(payload), retained, qos);
    }

    // Publish from ANY task: the port copies the message and hands it to the task that
    // owns the client. When `done` is not null the port calls done(ctx, payload, length,
    // sent) exactly once, on any task, when the message is sent or dropped, including
    // when this call itself fails.
    virtual bool queue_publish(const char* topic, const char* payload, int length,
                               bool retained, int qos, mqtt_publish_done_fn done,
                               void* ctx) = 0;

    // Subscribe one topic filter. Only on the task that owns the client, and never from
    // inside the receive callback: a client that waits for the SUBACK delivers other
    // messages meanwhile.
    virtual bool subscribe(const char* topic, int qos) = 0;

    virtual bool connected() = 0;

    // The client's error code for the last failed operation, for logs only.
    virtual int last_error() = 0;

 protected:
    ~MqttTransport() {}
};
