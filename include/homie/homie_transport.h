#pragma once
// The MQTT client as the Homie layer sees it: ebus_mqtt's MqttTransport plus the Homie
// queued publish that reports back to a Property. Device, Node, Property, the settable
// table and the controller publish and subscribe only through this interface; a port
// implements it over its own MQTT client (src/platform/mqtt_client.h on the ESP32).
#include <ebus/mqtt/transport.h>
#include <stdint.h>

class Property;

// Largest MQTT payload the firmware accepts or publishes: a $description is serialized
// into a buffer of this size, and the controller inbox must hold one. Define it in the
// build flags of the core and the port alike (not a source-only flag), so both agree.
#ifndef MAX_DATA_LEN
#define MAX_DATA_LEN 8192
#endif

// A port overrides one of the two queue_publish() overloads:
//   - MqttTransport's, with a completion callback (a new port), or
//   - the Property one below (a port written before ebus_mqtt).
// Each default forwards to, or stands in for, the other. Property calls the Property one.
class HomieTransport : public MqttTransport {
 public:
    using MqttTransport::queue_publish;

    // Default for a port that overrides only the Property overload: no queue, so the
    // message is refused and `done` is told so.
    bool queue_publish(const char* topic, const char* payload, int length, bool retained,
                       int qos, mqtt_publish_done_fn done, void* ctx) override;

    // Publish from ANY task at homie_qos(retained). When `source` is not null,
    // source->queued_publish_done(payload, length, sent) is called exactly once, when the
    // message is sent or dropped, including when this call itself fails. The default
    // forwards to the overload above with a callback that does that.
    virtual bool queue_publish(const char* topic, const char* payload, int length,
                               bool retained, Property* source);

 protected:
    ~HomieTransport() {}
};

// QoS for RETAINED Homie publications (property values + $state/$description).
// Homie 5 recommends QoS 2 (SHOULD), so it defaults to 2; the port may change it (the
// ESP32 firmware reads the `mqtt_qos` config key). Non-retained publications and /set are
// fixed at QoS 0 per the spec (a hard requirement), so homie_qos() returns 0 for them
// regardless. (C4)
extern uint8_t mqtt_qos;
inline int homie_qos(bool retained) { return retained ? (int)mqtt_qos : 0; }
