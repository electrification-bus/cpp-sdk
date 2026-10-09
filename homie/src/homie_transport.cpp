#include <ebus/homie/homie_transport.h>
#include <ebus/homie/homie_log.h>
#include <ebus/homie/Property.h>

uint8_t mqtt_qos = 2;   // QoS for retained Homie publications; Homie 5 recommends 2 (C4)

static void property_publish_done(void* ctx, const char* payload, int length, bool sent) {
    ((Property*)ctx)->queued_publish_done(payload, length, sent);
}

bool HomieTransport::queue_publish(const char* topic, const char* payload, int length,
                                   bool retained, int qos, mqtt_publish_done_fn done,
                                   void* ctx) {
    (void)retained;
    (void)qos;
    homie_logf("TRANSPORT: **ERROR -- this transport implements no queue_publish(); '%s' "
               "dropped\n", topic);
    if (done) done(ctx, payload, length, false);
    return false;
}

bool HomieTransport::queue_publish(const char* topic, const char* payload, int length,
                                   bool retained, Property* source) {
    return queue_publish(topic, payload, length, retained, homie_qos(retained),
                         source ? property_publish_done : nullptr, source);
}
