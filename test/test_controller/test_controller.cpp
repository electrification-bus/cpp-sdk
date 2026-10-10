// Host-side tests for the controller in the core, through a FakeTransport and a fake
// clock: what it subscribes and when, how it builds a device from $description, and the
// /set it publishes.

#include <unity.h>
#include <ebus/homie/controller.h>
#include "../support/fake_clock.h"
#include "../support/fake_transport.h"
#include "../support/log_capture.h"

static FakeTransport t;

// Hand one message to the controller the way the port's receive callback does.
static void deliver_n(const char* topic, const char* payload, size_t n) {
    char topic_buf[HOMIE_TOPIC_MAX + 1];
    static char payload_buf[2048];
    snprintf(topic_buf, sizeof(topic_buf), "%s", topic);
    memcpy(payload_buf, payload, n);
    controller_mqtt_callback(topic_buf, (uint8_t*)payload_buf, (unsigned)n);
}
static void deliver(const char* topic, const char* payload) {
    deliver_n(topic, payload, strlen(payload));
}

// A described device "dev1" with one property of each scalar datatype on node "n".
static void describe_typed_device(void) {
    controller_setup_discovery();
    controller_loop();
    deliver("homie/5/dev1/$state", "ready");
    deliver("homie/5/dev1/$description",
            "{\"homie\":\"5.0\",\"version\":1,\"name\":\"Mix\",\"type\":\"mix\","
            "\"nodes\":{\"n\":{\"name\":\"N\",\"properties\":{"
            "\"b\":{\"datatype\":\"boolean\"},\"i\":{\"datatype\":\"integer\"},"
            "\"f\":{\"datatype\":\"float\"},\"s\":{\"datatype\":\"string\"}}}}}");
    controller_loop();
}

void setUp(void) {
    t = FakeTransport();
    log_capture_bind();
    g_log.clear();
    fake_clock_bind();
    g_clock.reset();
    g_clock.now = 1000;
    controller_init(&t, "homie", false);
}
void tearDown(void) {}

static void test_discovery_subscribes_state_in_its_domain(void) {
    controller_setup_discovery();
    TEST_ASSERT_EQUAL_INT(0, t.count());   // scheduled, not made

    controller_loop();

    TEST_ASSERT_EQUAL_INT(1, t.count());
    TEST_ASSERT_EQUAL_INT(FakeTransport::SUBSCRIBE, t.at(0).kind);
    TEST_ASSERT_EQUAL_STRING("homie/5/+/$state", t.at(0).topic);
    TEST_ASSERT_EQUAL_INT(0, t.at(0).qos);

    controller_loop();                     // nothing pending
    TEST_ASSERT_EQUAL_INT(1, t.count());
}

static void test_failed_subscribe_is_retried_after_the_hold(void) {
    t.subscribe_result = false;
    controller_setup_discovery();
    controller_loop();
    TEST_ASSERT_EQUAL_INT(1, t.count());
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "(error -3); retrying"));

    t.subscribe_result = true;
    g_clock.now += 999;
    controller_loop();
    TEST_ASSERT_EQUAL_INT(1, t.count());   // still holding

    g_clock.now += 1;
    controller_loop();
    TEST_ASSERT_EQUAL_INT(2, t.count());
    TEST_ASSERT_EQUAL_STRING("homie/5/+/$state", t.at(1).topic);
}

static void test_new_device_is_subscribed_then_built_from_its_description(void) {
    controller_setup_discovery();
    controller_loop();
    t.clear();

    deliver("homie/5/dev1/$state", "ready");
    controller_loop();

    TEST_ASSERT_NOT_NULL(controller_get_device("dev1"));
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_READY, controller_effective_state("dev1"));
    TEST_ASSERT_EQUAL_INT(2, t.count());
    TEST_ASSERT_EQUAL_STRING("homie/5/dev1/$description", t.at(0).topic);
    TEST_ASSERT_EQUAL_STRING("homie/5/dev1/+/+", t.at(1).topic);

    deliver("homie/5/dev1/$description",
            "{\"homie\":\"5.0\",\"version\":1,\"name\":\"Plug\",\"type\":\"plug\","
            "\"nodes\":{\"sw\":{\"name\":\"Switch\",\"type\":\"switch\",\"properties\":"
            "{\"on\":{\"name\":\"On\",\"datatype\":\"boolean\",\"settable\":true}}}}}");
    deliver("homie/5/dev1/sw/on", "true");
    controller_loop();

    Property* on = controller_get_property("dev1", "sw", "on");
    TEST_ASSERT_NOT_NULL(on);
    TEST_ASSERT_TRUE(on->settable());
    TEST_ASSERT_EQUAL_STRING("true", on->value());
    TEST_ASSERT_EQUAL_STRING("plug", controller_get_device("dev1")->type());
    TEST_ASSERT_EQUAL_INT(2, t.count());   // building the device published nothing
}

static void test_set_property_publishes_a_non_retained_set(void) {
    controller_setup_discovery();
    controller_loop();
    deliver("homie/5/dev1/$state", "ready");
    deliver("homie/5/dev1/$description",
            "{\"homie\":\"5.0\",\"version\":1,\"name\":\"Plug\",\"type\":\"plug\","
            "\"nodes\":{\"sw\":{\"name\":\"Switch\",\"type\":\"switch\",\"properties\":"
            "{\"on\":{\"name\":\"On\",\"datatype\":\"boolean\",\"settable\":true},"
            "\"watts\":{\"name\":\"Power\",\"datatype\":\"float\"}}}}}");
    controller_loop();
    t.clear();

    TEST_ASSERT_TRUE(controller_set_property("dev1", "sw", "on", "false"));
    TEST_ASSERT_EQUAL_INT(1, t.count());
    TEST_ASSERT_EQUAL_INT(FakeTransport::PUBLISH, t.at(0).kind);
    TEST_ASSERT_EQUAL_STRING("homie/5/dev1/sw/on/set", t.at(0).topic);
    TEST_ASSERT_EQUAL_STRING("false", t.at(0).payload);
    TEST_ASSERT_FALSE(t.at(0).retained);
    TEST_ASSERT_EQUAL_INT(0, t.at(0).qos);

    TEST_ASSERT_FALSE(controller_set_property("dev1", "sw", "watts", "5"));   // not settable
    t.is_connected = false;
    TEST_ASSERT_FALSE(controller_set_property("dev1", "sw", "on", "true"));
    TEST_ASSERT_EQUAL_INT(1, t.count());
}

static void test_property_value_is_the_payload_as_published(void) {
    describe_typed_device();
    Property* b = controller_get_property("dev1", "n", "b");
    Property* i = controller_get_property("dev1", "n", "i");
    Property* f = controller_get_property("dev1", "n", "f");
    Property* s = controller_get_property("dev1", "n", "s");
    TEST_ASSERT_FALSE(f->has_value());   // described, no value yet

    deliver("homie/5/dev1/n/b", "true");
    deliver("homie/5/dev1/n/i", "-42");
    deliver("homie/5/dev1/n/f", "21.37");
    deliver("homie/5/dev1/n/s", "hello");
    controller_loop();

    TEST_ASSERT_TRUE(b->has_value());
    TEST_ASSERT_EQUAL_STRING("true", b->value());
    TEST_ASSERT_TRUE(b->getBoolValue());
    TEST_ASSERT_EQUAL_STRING("-42", i->value());
    TEST_ASSERT_EQUAL_INT64(-42, i->getIntValue());
    TEST_ASSERT_EQUAL_STRING("21.37", f->value());
    TEST_ASSERT_EQUAL_FLOAT(21.37f, f->getFloatValue());
    TEST_ASSERT_EQUAL_STRING("hello", s->value());

    deliver("homie/5/dev1/n/i", "9007199254740993");   // over 2^53: no float round trip
    controller_loop();
    TEST_ASSERT_EQUAL_STRING("9007199254740993", i->value());
    TEST_ASSERT_EQUAL_INT64(9007199254740993LL, i->getIntValue());
}

static void test_empty_payload_marks_the_value_absent_and_keeps_the_last(void) {
    describe_typed_device();
    deliver("homie/5/dev1/n/b", "true");
    deliver("homie/5/dev1/n/i", "7");
    deliver("homie/5/dev1/n/f", "21.37");
    deliver("homie/5/dev1/n/s", "hello");
    controller_loop();
    t.clear();

    deliver_n("homie/5/dev1/n/b", "", 0);
    deliver_n("homie/5/dev1/n/i", "", 0);
    deliver_n("homie/5/dev1/n/f", "", 0);
    deliver_n("homie/5/dev1/n/s", "", 0);
    controller_loop();

    Property* b = controller_get_property("dev1", "n", "b");
    Property* i = controller_get_property("dev1", "n", "i");
    Property* f = controller_get_property("dev1", "n", "f");
    Property* s = controller_get_property("dev1", "n", "s");
    TEST_ASSERT_FALSE(b->has_value());
    TEST_ASSERT_FALSE(i->has_value());
    TEST_ASSERT_FALSE(f->has_value());
    TEST_ASSERT_FALSE(s->has_value());
    // Not false, 0 or 0.000000: the last value received.
    TEST_ASSERT_EQUAL_STRING("true", b->value());
    TEST_ASSERT_TRUE(b->getBoolValue());
    TEST_ASSERT_EQUAL_STRING("7", i->value());
    TEST_ASSERT_EQUAL_INT64(7, i->getIntValue());
    TEST_ASSERT_EQUAL_STRING("21.37", f->value());
    TEST_ASSERT_EQUAL_STRING("hello", s->value());
    TEST_ASSERT_EQUAL_INT(0, t.count());   // the controller published no retraction itself

    deliver("homie/5/dev1/n/f", "19.5");   // a new value restores it
    controller_loop();
    TEST_ASSERT_TRUE(f->has_value());
    TEST_ASSERT_EQUAL_STRING("19.5", f->value());
}

static void test_empty_string_value_is_a_value(void) {
    describe_typed_device();
    deliver("homie/5/dev1/n/s", "hello");
    deliver("homie/5/dev1/n/b", "true");
    controller_loop();

    deliver_n("homie/5/dev1/n/s", "\0", 1);   // Homie 5's empty string
    deliver_n("homie/5/dev1/n/b", "\0", 1);   // no empty boolean: ignored
    controller_loop();

    Property* s = controller_get_property("dev1", "n", "s");
    Property* b = controller_get_property("dev1", "n", "b");
    TEST_ASSERT_TRUE(s->has_value());
    TEST_ASSERT_EQUAL_STRING("", s->value());
    TEST_ASSERT_TRUE(b->has_value());
    TEST_ASSERT_EQUAL_STRING("true", b->value());
}

static void test_device_at_walks_the_table_without_a_copy(void) {
    TEST_ASSERT_EQUAL_INT(0, controller_device_count());
    TEST_ASSERT_NULL(controller_device_at(0));

    controller_setup_discovery();
    controller_loop();
    deliver("homie/5/dev1/$state", "ready");
    deliver("homie/5/dev2/$state", "init");
    deliver("homie/5/dev3/$state", "ready");
    controller_loop();

    TEST_ASSERT_EQUAL_INT(3, controller_device_count());
    ControllerDevice copies[MAX_DISCOVERED_DEVICES];
    int n = controller_list_device_info(copies, MAX_DISCOVERED_DEVICES);
    TEST_ASSERT_EQUAL_INT(3, n);
    for (int i = 0; i < n; i++) {
        const ControllerDevice* d = controller_device_at(i);
        TEST_ASSERT_NOT_NULL(d);
        TEST_ASSERT_EQUAL_PTR(copies[i].device, d->device);
        TEST_ASSERT_EQUAL_PTR(controller_get_device_info(d->device->getId()), d);
    }
    TEST_ASSERT_EQUAL_STRING("dev2", controller_device_at(1)->device->getId());
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_INIT, controller_device_at(1)->state);
    TEST_ASSERT_NULL(controller_device_at(3));
    TEST_ASSERT_NULL(controller_device_at(-1));

    deliver("homie/5/dev2/$state", "ready");   // the pointer sees updates
    controller_loop();
    TEST_ASSERT_EQUAL_INT(DEVICE_STATE_READY, controller_device_at(1)->state);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_discovery_subscribes_state_in_its_domain);
    RUN_TEST(test_failed_subscribe_is_retried_after_the_hold);
    RUN_TEST(test_new_device_is_subscribed_then_built_from_its_description);
    RUN_TEST(test_set_property_publishes_a_non_retained_set);
    RUN_TEST(test_property_value_is_the_payload_as_published);
    RUN_TEST(test_empty_payload_marks_the_value_absent_and_keeps_the_last);
    RUN_TEST(test_empty_string_value_is_a_value);
    RUN_TEST(test_device_at_walks_the_table_without_a_copy);
    return UNITY_END();
}
