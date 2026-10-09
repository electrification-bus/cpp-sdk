// SPDX-License-Identifier: MIT
// The reconnect order (ebus/mqtt/reconnect.h): flush, re-subscribe, notify. Builds against
// ebus_mqtt alone.
#include <unity.h>
#include <ebus/mqtt/reconnect.h>
#include <string.h>

// Records each step as one letter: 'p' per publish sent, 's' for the re-subscribe,
// 'n' (first connect) or 'r' (later) for the notify.
struct Port {
    char steps[32];
    int n = 0;
    int send_ok = 100;          // sends that succeed before the link drops
    bool resubscribe_ok = true;

    void add(char c) {
        if (n < (int)sizeof(steps) - 1) steps[n++] = c;
        steps[n] = '\0';
    }
    static bool send(void* ctx, const char*, const char*, int, bool, int) {
        Port* p = (Port*)ctx;
        if (p->send_ok-- <= 0) return false;
        p->add('p');
        return true;
    }
    static bool resubscribe(void* ctx) {
        Port* p = (Port*)ctx;
        p->add('s');
        return p->resubscribe_ok;
    }
    static void notify(void* ctx, bool first) { ((Port*)ctx)->add(first ? 'n' : 'r'); }

    MqttConnectSteps hooks() { return MqttConnectSteps{send, resubscribe, notify, this}; }
};

static PublishHold hold;
static Port port;

void setUp(void) {
    hold.clear();
    port = Port();
    port.steps[0] = '\0';
}
void tearDown(void) {}

static void test_flush_then_resubscribe_then_notify(void) {
    hold.hold("a", "1", 1, true, 2);
    hold.hold("b", "2", 1, true, 2);
    MqttConnectReport report;
    TEST_ASSERT_TRUE(mqtt_after_connect(&hold, port.hooks(), true, &report));
    TEST_ASSERT_EQUAL_STRING("ppsn", port.steps);
    TEST_ASSERT_EQUAL_INT(2, report.held);
    TEST_ASSERT_EQUAL_INT(2, report.flushed);
    TEST_ASSERT_EQUAL_INT(0, hold.count());
}

static void test_interrupted_flush_stops_before_resubscribe(void) {
    hold.hold("a", "1", 1, true, 2);
    hold.hold("b", "2", 1, true, 2);
    port.send_ok = 1;
    MqttConnectReport report;
    TEST_ASSERT_FALSE(mqtt_after_connect(&hold, port.hooks(), false, &report));
    TEST_ASSERT_EQUAL_STRING("p", port.steps);
    TEST_ASSERT_EQUAL_INT(2, report.held);
    TEST_ASSERT_EQUAL_INT(1, report.flushed);
    TEST_ASSERT_EQUAL_INT(1, hold.count());
}

static void test_link_lost_during_resubscribe_skips_notify(void) {
    port.resubscribe_ok = false;
    TEST_ASSERT_FALSE(mqtt_after_connect(&hold, port.hooks(), false));
    TEST_ASSERT_EQUAL_STRING("s", port.steps);
}

static void test_no_hold_and_optional_steps(void) {
    TEST_ASSERT_TRUE(mqtt_after_connect(nullptr, port.hooks(), false));
    TEST_ASSERT_EQUAL_STRING("sr", port.steps);
    MqttConnectSteps bare = {Port::send, nullptr, nullptr, &port};
    TEST_ASSERT_TRUE(mqtt_after_connect(&hold, bare, true));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_flush_then_resubscribe_then_notify);
    RUN_TEST(test_interrupted_flush_stops_before_resubscribe);
    RUN_TEST(test_link_lost_during_resubscribe_skips_notify);
    RUN_TEST(test_no_hold_and_optional_steps);
    return UNITY_END();
}
