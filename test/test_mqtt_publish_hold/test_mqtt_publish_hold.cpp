// SPDX-License-Identifier: MIT
// The disconnected-link rules (ebus/mqtt/publish_hold.h), without a broker. Builds against
// ebus_mqtt alone.
#include <unity.h>
#include <ebus/mqtt/publish_hold.h>
#include <stdio.h>
#include <string.h>

struct Sent {
    char topic[EBUS_MQTT_HOLD_TOPIC_MAX + 1];
    char payload[16];
    int length;
    bool retained;
    int qos;
};

struct Sink {
    static const int MAX = EBUS_MQTT_HOLD_ENTRIES + 8;
    Sent sent[MAX];
    int count = 0;
    int fail_after = -1;   // refuse the send after this many

    static bool send(void* ctx, const char* topic, const char* payload, int length,
                     bool retained, int qos) {
        Sink* s = (Sink*)ctx;
        if (s->fail_after >= 0 && s->count >= s->fail_after) return false;
        if (s->count >= MAX) return false;
        Sent& e = s->sent[s->count++];
        snprintf(e.topic, sizeof(e.topic), "%s", topic);
        int n = length < (int)sizeof(e.payload) ? length : (int)sizeof(e.payload);
        memcpy(e.payload, payload, (size_t)n);
        e.length = length;
        e.retained = retained;
        e.qos = qos;
        return true;
    }
};

static PublishHold hold;   // large: keep it off the stack
static Sink sink;

void setUp(void) {
    hold.clear();
    sink = Sink();
}
void tearDown(void) {}

static void assert_sent(int i, const char* topic, const char* payload) {
    TEST_ASSERT_EQUAL_STRING(topic, sink.sent[i].topic);
    TEST_ASSERT_EQUAL_INT((int)strlen(payload), sink.sent[i].length);
    TEST_ASSERT_EQUAL_MEMORY(payload, sink.sent[i].payload, strlen(payload));
}

static void test_rules(void) {
    TEST_ASSERT_EQUAL_INT(PublishHold::HELD, hold.hold("a/v", "1", 1, true, 2));
    TEST_ASSERT_EQUAL_INT(PublishHold::HELD, hold.hold("e/x", "ev1", 3, false, 1));
    TEST_ASSERT_EQUAL_INT(PublishHold::DROPPED_QOS0, hold.hold("e/q0", "gone", 4, false, 0));
    TEST_ASSERT_EQUAL_INT(PublishHold::HELD, hold.hold("b/v", "x", 1, true, 1));
    // Events never merge; a retained value is newest per topic.
    TEST_ASSERT_EQUAL_INT(PublishHold::HELD, hold.hold("e/x", "ev2", 3, false, 2));
    TEST_ASSERT_EQUAL_INT(PublishHold::REPLACED, hold.hold("a/v", "2", 1, true, 2));
    TEST_ASSERT_EQUAL_INT(4, hold.count());

    TEST_ASSERT_EQUAL_INT(4, hold.flush(Sink::send, &sink));
    TEST_ASSERT_EQUAL_INT(0, hold.count());
    // The replaced retained value moved to the back.
    TEST_ASSERT_EQUAL_INT(4, sink.count);
    assert_sent(0, "e/x", "ev1");
    assert_sent(1, "b/v", "x");
    assert_sent(2, "e/x", "ev2");
    assert_sent(3, "a/v", "2");
    TEST_ASSERT_TRUE(sink.sent[3].retained);
    TEST_ASSERT_EQUAL_INT(2, sink.sent[3].qos);
    TEST_ASSERT_FALSE(sink.sent[0].retained);
    TEST_ASSERT_EQUAL_INT(1, sink.sent[0].qos);
}

static void test_empty_string_value_kept(void) {
    const char nul = 0;
    TEST_ASSERT_EQUAL_INT(PublishHold::HELD, hold.hold("a/s", &nul, 1, true, 2));
    hold.flush(Sink::send, &sink);
    TEST_ASSERT_EQUAL_INT(1, sink.count);
    TEST_ASSERT_EQUAL_INT(1, sink.sent[0].length);
    TEST_ASSERT_EQUAL_INT8(0, sink.sent[0].payload[0]);
}

static void test_bounded_evicts_oldest(void) {
    uint32_t evicted = hold.evicted();
    char topic[32];
    for (int i = 0; i < EBUS_MQTT_HOLD_ENTRIES; i++) {
        snprintf(topic, sizeof(topic), "t/%d", i);
        TEST_ASSERT_EQUAL_INT(PublishHold::HELD, hold.hold(topic, "v", 1, true, 2));
    }
    for (int i = EBUS_MQTT_HOLD_ENTRIES; i < EBUS_MQTT_HOLD_ENTRIES + 3; i++) {
        snprintf(topic, sizeof(topic), "t/%d", i);
        TEST_ASSERT_EQUAL_INT(PublishHold::EVICTED_OLDEST, hold.hold(topic, "v", 1, true, 2));
    }
    TEST_ASSERT_EQUAL_INT(EBUS_MQTT_HOLD_ENTRIES, hold.count());
    TEST_ASSERT_EQUAL_UINT32(3, hold.evicted() - evicted);
    hold.flush(Sink::send, &sink);
    TEST_ASSERT_EQUAL_INT(EBUS_MQTT_HOLD_ENTRIES, sink.count);
    TEST_ASSERT_EQUAL_STRING("t/3", sink.sent[0].topic);
    snprintf(topic, sizeof(topic), "t/%d", EBUS_MQTT_HOLD_ENTRIES + 2);
    TEST_ASSERT_EQUAL_STRING(topic, sink.sent[sink.count - 1].topic);
}

static void test_too_large_dropped(void) {
    static char big[EBUS_MQTT_HOLD_PAYLOAD_MAX + 1];
    TEST_ASSERT_EQUAL_INT(PublishHold::DROPPED_TOO_LARGE,
                          hold.hold("d/$description", big, sizeof(big), true, 2));
    static char long_topic[EBUS_MQTT_HOLD_TOPIC_MAX + 2];
    memset(long_topic, 'x', sizeof(long_topic) - 1);
    TEST_ASSERT_EQUAL_INT(PublishHold::DROPPED_TOO_LARGE,
                          hold.hold(long_topic, "v", 1, true, 2));
    TEST_ASSERT_EQUAL_INT(0, hold.count());
}

static void test_interrupted_flush_keeps_the_rest(void) {
    hold.hold("a", "1", 1, true, 2);
    hold.hold("b", "2", 1, true, 2);
    hold.hold("c", "3", 1, true, 2);
    sink.fail_after = 1;
    TEST_ASSERT_EQUAL_INT(1, hold.flush(Sink::send, &sink));
    TEST_ASSERT_EQUAL_INT(2, hold.count());
    // A newer value for a still-held topic replaces it.
    TEST_ASSERT_EQUAL_INT(PublishHold::REPLACED, hold.hold("c", "4", 1, true, 2));
    sink = Sink();
    TEST_ASSERT_EQUAL_INT(2, hold.flush(Sink::send, &sink));
    TEST_ASSERT_EQUAL_INT(2, sink.count);
    assert_sent(0, "b", "2");
    assert_sent(1, "c", "4");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_rules);
    RUN_TEST(test_empty_string_value_kept);
    RUN_TEST(test_bounded_evicts_oldest);
    RUN_TEST(test_too_large_dropped);
    RUN_TEST(test_interrupted_flush_keeps_the_rest);
    return UNITY_END();
}
