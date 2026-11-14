// Host-side tests for the controller running out of memory: a device it cannot allocate
// is not tracked, and one it runs out of memory building from $description is rejected
// with a log line and its messages ignored, instead of aborting on an uncaught bad_alloc.
//
// This suite replaces the global nothrow operator new, which the controller path uses,
// with one that fails on demand; it is a suite of its own so no other suite gets it.

#include <unity.h>
#include <ebus/homie/controller.h>
#include <new>
#include <stdlib.h>
#include "../support/fake_clock.h"
#include "../support/fake_transport.h"
#include "../support/log_capture.h"

// Allocations to let through before failing; -1 never fails.
static int g_allow = -1;

void* operator new(size_t n, const std::nothrow_t&) noexcept {
    if (g_allow == 0) return nullptr;
    if (g_allow > 0) g_allow--;
    return malloc(n);
}

static FakeTransport t;

static void deliver(const char* topic, const char* payload) {
    char topic_buf[HOMIE_TOPIC_MAX + 1];
    static char payload_buf[2048];
    snprintf(topic_buf, sizeof(topic_buf), "%s", topic);
    size_t n = strlen(payload);
    memcpy(payload_buf, payload, n);
    controller_mqtt_callback(topic_buf, (uint8_t*)payload_buf, (unsigned)n);
}

// One node "n" with three properties.
static const char* DESCRIPTION =
    "{\"homie\":\"5.0\",\"version\":1,\"name\":\"Big\",\"type\":\"big\","
    "\"nodes\":{\"n\":{\"name\":\"N\",\"properties\":{"
    "\"a\":{\"datatype\":\"integer\"},\"b\":{\"datatype\":\"integer\"},"
    "\"c\":{\"datatype\":\"integer\"}}}}}";

void setUp(void) {
    g_allow = -1;
    t = FakeTransport();
    log_capture_bind();
    g_log.clear();
    fake_clock_bind();
    g_clock.reset();
    g_clock.now = 1000;
    controller_init(&t, "homie", false);
    controller_setup_discovery();
    controller_loop();
}
void tearDown(void) { g_allow = -1; }

static void test_device_that_cannot_be_allocated_is_not_tracked(void) {
    g_allow = 0;
    deliver("homie/5/dev1/$state", "ready");
    controller_loop();
    TEST_ASSERT_NULL(controller_get_device("dev1"));
    TEST_ASSERT_NOT_NULL(strstr(g_log.last_error, "no memory for device dev1"));

    g_allow = -1;   // memory again: the next $state tracks it
    deliver("homie/5/dev1/$state", "ready");
    controller_loop();
    TEST_ASSERT_NOT_NULL(controller_get_device("dev1"));
}

static void test_device_out_of_memory_mid_description_is_rejected(void) {
    deliver("homie/5/dev1/$state", "ready");
    controller_loop();
    TEST_ASSERT_NOT_NULL(controller_get_device("dev1"));

    g_allow = 2;    // the node and property "a", then nothing
    deliver("homie/5/dev1/$description", DESCRIPTION);
    controller_loop();
    TEST_ASSERT_NOT_NULL(strstr(g_log.last_error, "out of memory building device dev1"));
    TEST_ASSERT_NULL(controller_get_property("dev1", "n", "b"));

    // Its property messages and a repeat of its $description are ignored, without a line.
    g_allow = -1;
    int lines = g_log.lines;
    deliver("homie/5/dev1/n/c", "7");
    deliver("homie/5/dev1/$description", DESCRIPTION);
    controller_loop();
    TEST_ASSERT_EQUAL_INT(lines, g_log.lines);
    TEST_ASSERT_NULL(controller_get_property("dev1", "n", "c"));
}

static void test_other_devices_are_tracked_after_a_rejection(void) {
    deliver("homie/5/dev1/$state", "ready");
    controller_loop();
    g_allow = 1;    // the node, then nothing
    deliver("homie/5/dev1/$description", DESCRIPTION);
    controller_loop();
    g_allow = -1;

    deliver("homie/5/dev2/$state", "ready");
    deliver("homie/5/dev2/$description", DESCRIPTION);
    deliver("homie/5/dev2/n/c", "7");
    controller_loop();
    Property* c = controller_get_property("dev2", "n", "c");
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_STRING("7", c->value());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_device_that_cannot_be_allocated_is_not_tracked);
    RUN_TEST(test_device_out_of_memory_mid_description_is_rejected);
    RUN_TEST(test_other_devices_are_tracked_after_a_rejection);
    return UNITY_END();
}
