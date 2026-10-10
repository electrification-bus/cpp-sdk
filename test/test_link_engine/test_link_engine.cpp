// Host-side tests for EbusLink (link/src/link.cpp) through a FakeTransport, a fake clock and
// the real settable table and controller: device-mode local and remote ends, controller
// links binding * against the discovery cache, the controller opt-in, retraction, change
// detection and backoff.
#include <unity.h>
#include <ebus/homie/Device.h>
#include <ebus/homie/Node.h>
#include <ebus/homie/Property.h>
#include <ebus/homie/controller.h>
#include <ebus/homie/homie_settable.h>
#include <ebus/link/link.h>
#include <string.h>
#include "../support/fake_clock.h"
#include "../support/fake_transport.h"
#include "../support/log_capture.h"

static FakeTransport t;
static subscribed_settable_property_t _table[8];

// The device "dev": env/temp and env/hum (float readings), disp/line (settable string).
static Device dev;
static Property temp, hum, line;
static bool _built = false;

static const char* LINE_SET = "homie/5/dev/disp/line/set";
static const char* LINE_T = "homie/5/dev/disp/line";

static void build_device(void) {
    if (_built) return;
    _built = true;
    dev.init("Dev", "dev", "generic", &t);
    Node* env = dev.addNode("env", "Env", "generic");
    env->addProperty(&temp, "temp", "Temp", "float");
    env->addProperty(&hum, "hum", "Hum", "float");
    Node* disp = dev.addNode("disp", "Disp", "generic");
    disp->addProperty(&line, "line", "Line", "string", "", true);
}

void setUp(void) {
    t = FakeTransport();
    log_capture_bind();
    g_log.clear();
    fake_clock_bind();
    g_clock.reset();
    g_clock.now = 1000;
    settable_table_bind(_table, 8, &t, nullptr);
    build_device();
    temp.forget_value();
    hum.forget_value();
    line.setValue("");
    line.subscribe();   // registers disp/line/set in the fresh table
    controller_reset();
    controller_init(&t, "homie", false);
    t.clear();
}
void tearDown(void) {}

// Publishes to `topic` among the transport's records.
static int published(const char* topic) {
    int n = 0;
    for (int i = 0; i < t.count() && i < FakeTransport::MAX_RECORDS; i++) {
        if (t.at(i).kind == FakeTransport::PUBLISH && strcmp(t.at(i).topic, topic) == 0) n++;
    }
    return n;
}
static const char* last_payload(const char* topic) {
    const char* p = nullptr;
    for (int i = 0; i < t.count() && i < FakeTransport::MAX_RECORDS; i++) {
        if (t.at(i).kind == FakeTransport::PUBLISH && strcmp(t.at(i).topic, topic) == 0) {
            p = t.at(i).payload;
        }
    }
    return p;
}
static void tick(EbusLink& l, uint32_t ms) {
    g_clock.now += ms;
    l.loop();
}

// ── device mode ─────────────────────────────────────────────────────────────────────────

static void test_local_sources_drive_a_local_target(void) {
    EbusLink l("climate", "env/temp, env/hum", "disp/line", "%1 C|%2 %%", 1);
    TEST_ASSERT_TRUE(l.setup(EbusLink::DEVICE, &dev));
    TEST_ASSERT_EQUAL_STRING(LINE_SET, l.target());

    temp.setValue(21.37f);
    l.loop();
    TEST_ASSERT_NULL(l.last_sent());   // hum has no value yet: nothing is sent
    TEST_ASSERT_EQUAL_INT(0, published(LINE_T));

    hum.setValue(48.25f);
    tick(l, 999);
    TEST_ASSERT_NULL(l.last_sent());   // local sources are read on the interval
    tick(l, 1);
    TEST_ASSERT_EQUAL_STRING("21.4 C|48.2 %", l.last_sent());
    TEST_ASSERT_EQUAL_STRING("21.4 C|48.2 %", line.value());
    TEST_ASSERT_EQUAL_INT(1, published(LINE_T));
}

static void test_target_is_written_only_when_the_text_changes(void) {
    EbusLink l("t", "env/temp", "disp/line", "%s", 0);
    TEST_ASSERT_TRUE(l.setup(EbusLink::DEVICE, &dev));
    temp.setValue(21.2f);
    l.loop();
    temp.setValue(20.9f);   // rounds to the same text
    tick(l, 1000);
    tick(l, 1000);
    TEST_ASSERT_EQUAL_INT(1, published(LINE_T));
    temp.setValue(22.0f);
    tick(l, 1000);
    TEST_ASSERT_EQUAL_INT(2, published(LINE_T));
    TEST_ASSERT_EQUAL_STRING("22", line.value());
}

static void test_local_retraction_keeps_the_last_good_value(void) {
    EbusLink l("t", "env/temp", "disp/line", "%s", 1);
    TEST_ASSERT_TRUE(l.setup(EbusLink::DEVICE, &dev));
    temp.setValue(21.0f);
    l.loop();
    temp.forget_value();
    tick(l, 1000);
    TEST_ASSERT_TRUE(l.source(0).has_value);
    TEST_ASSERT_EQUAL_STRING("21.0", l.source(0).value);
    TEST_ASSERT_EQUAL_INT(1, published(LINE_T));
}

static void test_remote_source_arrives_through_settable_dispatch(void) {
    EbusLink l("r", "other/env/temp", "disp/line", "%s", 1);
    TEST_ASSERT_TRUE(l.setup(EbusLink::DEVICE, &dev));
    const char* src = "homie/5/other/env/temp";
    TEST_ASSERT_TRUE(settable_is_registered(src));
    const FakeTransport::Record* sub = t.nth(FakeTransport::SUBSCRIBE, 0);
    TEST_ASSERT_NOT_NULL(sub);
    TEST_ASSERT_EQUAL_STRING(src, sub->topic);
    TEST_ASSERT_EQUAL_INT(0, sub->qos);

    TEST_ASSERT_TRUE(settable_dispatch(src, "19.96"));
    g_clock.now += 1;   // not due: a remote value is acted on at once
    l.loop();
    TEST_ASSERT_EQUAL_STRING("20.0", line.value());

    // A retraction keeps the last good value and is not forwarded.
    TEST_ASSERT_TRUE(settable_dispatch(src, ""));
    tick(l, 1000);
    TEST_ASSERT_EQUAL_STRING("19.96", l.source(0).value);
    TEST_ASSERT_EQUAL_INT(1, published(LINE_T));

    TEST_ASSERT_TRUE(settable_dispatch(src, "18"));
    tick(l, 1);
    TEST_ASSERT_EQUAL_STRING("18.0", line.value());
}

static void test_remote_target_gets_a_non_retained_set(void) {
    EbusLink l("r", "env/temp", "other/disp/line", "%s", 0);
    TEST_ASSERT_TRUE(l.setup(EbusLink::DEVICE, &dev));
    TEST_ASSERT_EQUAL_STRING("homie/5/other/disp/line/set", l.target());
    temp.setValue(7.0f);
    l.loop();
    const FakeTransport::Record* r = t.nth(FakeTransport::PUBLISH, 0);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("homie/5/other/disp/line/set", r->topic);
    TEST_ASSERT_EQUAL_STRING("7", r->payload);
    TEST_ASSERT_FALSE(r->retained);
    TEST_ASSERT_EQUAL_INT(0, r->qos);
}

static void test_failed_delivery_backs_off(void) {
    EbusLink l("r", "env/temp", "other/disp/line", "%s", 0, 1000);
    TEST_ASSERT_TRUE(l.setup(EbusLink::DEVICE, &dev));
    const char* set = "homie/5/other/disp/line/set";
    t.is_connected = false;
    temp.setValue(7.0f);
    l.loop();                                   // attempt 1 fails: wait 1000
    TEST_ASSERT_EQUAL_INT(1, published(set));
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "failed; retrying, backing off"));
    tick(l, 1000);                              // attempt 2: wait 2000
    TEST_ASSERT_EQUAL_INT(2, published(set));
    tick(l, 1000);
    TEST_ASSERT_EQUAL_INT(2, published(set));   // still waiting
    tick(l, 1000);                              // attempt 3: wait 4000
    TEST_ASSERT_EQUAL_INT(3, published(set));
    t.is_connected = true;
    tick(l, 3000);
    TEST_ASSERT_EQUAL_INT(3, published(set));
    tick(l, 1000);
    TEST_ASSERT_EQUAL_INT(4, published(set));
    TEST_ASSERT_EQUAL_STRING("7", l.last_sent());
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "delivered to homie/5/other/disp/line/set"));
}

static void test_one_remote_topic_feeds_one_link_source(void) {
    EbusLink a("a", "other/env/temp", "disp/line");
    EbusLink b("b", "other/env/temp", "disp/line");
    EbusLink c("c", "x/env/temp, x/env/temp", "disp/line");
    TEST_ASSERT_TRUE(a.setup(EbusLink::DEVICE, &dev));
    TEST_ASSERT_FALSE(b.setup(EbusLink::DEVICE, &dev));
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "already watched by another link source"));
    TEST_ASSERT_FALSE(c.setup(EbusLink::DEVICE, &dev));
    TEST_ASSERT_FALSE(b.enabled());
}

static bool accept_any(void*, const char*) { return true; }

static void test_full_settable_table_disables_the_link(void) {
    subscribed_settable_property_t small[1];
    settable_table_bind(small, 1, &t, nullptr);
    subscribe_settable_handler("homie/5/x/y/z", accept_any, nullptr);
    EbusLink l("full", "other/env/temp", "disp/line");
    TEST_ASSERT_FALSE(l.setup(EbusLink::DEVICE, &dev));
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "could not be registered; link disabled"));
}

static void test_device_mode_refuses_patterns_and_bad_references(void) {
    EbusLink a("a", "*/env/temp", "disp/line");
    TEST_ASSERT_FALSE(a.setup(EbusLink::DEVICE, &dev));
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "only a controller link resolves it"));
    EbusLink b("b", "env/temp", "*/disp/line");
    TEST_ASSERT_FALSE(b.setup(EbusLink::DEVICE, &dev));
    EbusLink c("c", "env/temp, env/hum, env/temp, env/hum", "disp/line");
    TEST_ASSERT_FALSE(c.setup(EbusLink::DEVICE, &dev));
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "too many sources"));
    EbusLink d("d", "env/te*", "disp/line");
    TEST_ASSERT_FALSE(d.setup(EbusLink::DEVICE, &dev));
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "* in the property is never resolved"));
    EbusLink e("e", "env/temp", "disp/line");
    TEST_ASSERT_FALSE(e.setup(EbusLink::DEVICE, nullptr));
}

static void test_missing_local_source_is_reported_once(void) {
    EbusLink l("m", "env/nope", "disp/line");
    TEST_ASSERT_TRUE(l.setup(EbusLink::DEVICE, &dev));
    l.loop();
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "source env/nope not found on this device"));
    int lines = g_log.lines;
    tick(l, 1000);
    tick(l, 1000);
    TEST_ASSERT_EQUAL_INT(lines, g_log.lines);
}

// ── controller mode ─────────────────────────────────────────────────────────────────────

static void deliver(const char* topic, const char* payload) {
    char topic_buf[HOMIE_TOPIC_MAX + 1];
    static char payload_buf[1024];
    snprintf(topic_buf, sizeof(topic_buf), "%s", topic);
    size_t n = strlen(payload);
    memcpy(payload_buf, payload, n);
    controller_mqtt_callback(topic_buf, (uint8_t*)payload_buf, (unsigned)n);
}

static void announce(const char* id) {
    char topic[HOMIE_TOPIC_MAX + 1];
    snprintf(topic, sizeof(topic), "homie/5/%s/$state", id);
    deliver(topic, "ready");
    controller_loop();
}

// $description with one node `node` holding `prop` (settable or not).
static void describe(const char* id, const char* node, const char* prop, bool settable) {
    char topic[HOMIE_TOPIC_MAX + 1];
    char desc[512];
    snprintf(topic, sizeof(topic), "homie/5/%s/$description", id);
    snprintf(desc, sizeof(desc),
             "{\"homie\":\"5.0\",\"version\":1,\"name\":\"D\",\"type\":\"t\",\"nodes\":{\"%s\":"
             "{\"name\":\"N\",\"properties\":{\"%s\":{\"datatype\":\"string\"%s}}}}}",
             node, prop, settable ? ",\"settable\":true" : "");
    deliver(topic, desc);
    controller_loop();
}

static void value(const char* id, const char* node, const char* prop, const char* v) {
    char topic[HOMIE_TOPIC_MAX + 1];
    snprintf(topic, sizeof(topic), "homie/5/%s/%s/%s", id, node, prop);
    deliver(topic, v);
    controller_loop();
}

// Every controller test runs after test_controller_mode_needs_the_opt_in, which needs the
// opt-in not to have been called yet.
static void start_discovery(void) {
    ebus_link_enable_controller();
    controller_setup_discovery();
    controller_loop();
}

static void test_controller_mode_needs_the_opt_in(void) {
    EbusLink l("c", "s/env/temp", "d/display/line");
    TEST_ASSERT_FALSE(l.setup(EbusLink::CONTROLLER));
    TEST_ASSERT_FALSE(l.enabled());
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "needs ebus_link_enable_controller() first"));
    ebus_link_enable_controller();
    TEST_ASSERT_TRUE(l.setup(EbusLink::CONTROLLER));
}

static void test_controller_link_binds_patterns_once_discovery_settles(void) {
    start_discovery();
    EbusLink l("p", "*/environment-*/air-pressure", "*/display/line", "%s", 0);
    TEST_ASSERT_TRUE(l.setup(EbusLink::CONTROLLER));
    TEST_ASSERT_FALSE(l.target_bound());

    l.loop();
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "waiting for discovery to settle: no devices yet"));

    announce("sensor1");
    describe("sensor1", "environment-bme280", "air-pressure", false);
    value("sensor1", "environment-bme280", "air-pressure", "1013.4");
    announce("display1");   // known, not described yet
    int lines = g_log.lines;
    tick(l, 1000);
    TEST_ASSERT_EQUAL_INT(lines, g_log.lines);   // the wait was said when it started
    tick(l, EBUS_LINK_REMINDER_MS);
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "1 of 2 device(s) without $description (display1)"));
    TEST_ASSERT_TRUE(l.source(0).wild);

    describe("display1", "display", "line", true);
    t.clear();
    tick(l, 1000);
    TEST_ASSERT_FALSE(l.source(0).wild);
    TEST_ASSERT_EQUAL_STRING("sensor1", l.source(0).dev);
    TEST_ASSERT_EQUAL_STRING("environment-bme280", l.source(0).node);
    TEST_ASSERT_TRUE(l.target_bound());
    TEST_ASSERT_EQUAL_STRING("display1/display/line", l.target());
    TEST_ASSERT_EQUAL_STRING("1013", last_payload("homie/5/display1/display/line/set"));
}

static void test_controller_pattern_matching_two_nodes_is_refused(void) {
    start_discovery();
    announce("a4cf12e8d0b4");
    describe("a4cf12e8d0b4", "environment-sht20", "temperature", false);
    value("a4cf12e8d0b4", "environment-sht20", "temperature", "21");
    announce("b0b21c90f570");
    describe("b0b21c90f570", "environment-bme280", "temperature", false);
    value("b0b21c90f570", "environment-bme280", "temperature", "22");
    announce("disp");
    describe("disp", "display", "line", true);

    EbusLink amb("amb", "*/environment-*/temperature", "disp/display/line");
    TEST_ASSERT_TRUE(amb.setup(EbusLink::CONTROLLER));
    t.clear();
    amb.loop();
    TEST_ASSERT_TRUE(amb.source(0).refused);
    TEST_ASSERT_NOT_NULL(strstr(g_log.last,
                                "matches more than one node (a4cf12e8d0b4/environment-sht20, "
                                "b0b21c90f570/environment-bme280); not bound"));
    tick(amb, 1000);
    TEST_ASSERT_EQUAL_INT(0, t.count_of(FakeTransport::PUBLISH));

    // The device segment narrows it.
    EbusLink one("one", "a4cf*/environment-*/temperature", "disp/display/line");
    TEST_ASSERT_TRUE(one.setup(EbusLink::CONTROLLER));
    tick(one, 1000);
    TEST_ASSERT_EQUAL_STRING("a4cf12e8d0b4", one.source(0).dev);
    TEST_ASSERT_EQUAL_STRING("21", last_payload("homie/5/disp/display/line/set"));
}

static void test_controller_target_pattern_needs_a_settable_property(void) {
    start_discovery();
    announce("s");
    describe("s", "env", "line", false);   // has "line", read-only
    value("s", "env", "line", "x");
    announce("d");
    describe("d", "display", "line", true);
    EbusLink l("t", "s/env/line", "*/*/line");
    TEST_ASSERT_TRUE(l.setup(EbusLink::CONTROLLER));
    l.loop();
    TEST_ASSERT_EQUAL_STRING("d/display/line", l.target());
    TEST_ASSERT_EQUAL_STRING("x", last_payload("homie/5/d/display/line/set"));
}

static void test_controller_retraction_does_not_reach_the_target(void) {
    start_discovery();
    announce("s");
    describe("s", "env", "temp", false);
    value("s", "env", "temp", "20.5");
    announce("d");
    describe("d", "display", "line", true);
    EbusLink l("r", "s/env/temp", "d/display/line");
    TEST_ASSERT_TRUE(l.setup(EbusLink::CONTROLLER));
    l.loop();
    const char* set = "homie/5/d/display/line/set";
    TEST_ASSERT_EQUAL_INT(1, published(set));

    value("s", "env", "temp", "");   // the retained value removed
    TEST_ASSERT_FALSE(controller_get_property("s", "env", "temp")->has_value());
    tick(l, 1000);
    tick(l, 1000);
    TEST_ASSERT_EQUAL_INT(1, published(set));
    TEST_ASSERT_EQUAL_STRING("20.5", l.source(0).value);

    value("s", "env", "temp", "21");
    tick(l, 1000);
    TEST_ASSERT_EQUAL_INT(2, published(set));
    TEST_ASSERT_EQUAL_STRING("21", last_payload(set));
}

static void test_controller_holds_until_ready_and_resends_on_return(void) {
    start_discovery();
    announce("s");
    describe("s", "env", "temp", false);
    value("s", "env", "temp", "5");
    announce("d");
    describe("d", "display", "line", true);
    EbusLink l("g", "s/env/temp", "d/display/line");
    TEST_ASSERT_TRUE(l.setup(EbusLink::CONTROLLER));
    const char* set = "homie/5/d/display/line/set";

    deliver("homie/5/d/$state", "lost");
    controller_loop();
    l.loop();
    TEST_ASSERT_EQUAL_INT(0, published(set));

    deliver("homie/5/d/$state", "ready");
    controller_loop();
    tick(l, 1000);
    TEST_ASSERT_EQUAL_INT(1, published(set));
    tick(l, 1000);
    TEST_ASSERT_EQUAL_INT(1, published(set));   // unchanged text

    deliver("homie/5/d/$state", "lost");
    controller_loop();
    tick(l, 1000);
    deliver("homie/5/d/$state", "ready");
    controller_loop();
    tick(l, 1000);
    TEST_ASSERT_EQUAL_INT(2, published(set));   // same text, sent again on return
}

static void test_controller_links_name_a_device_at_both_ends(void) {
    ebus_link_enable_controller();
    EbusLink a("a", "env/temp", "d/display/line");
    TEST_ASSERT_FALSE(a.setup(EbusLink::CONTROLLER));
    TEST_ASSERT_NOT_NULL(strstr(g_log.last, "(controller link)"));
    EbusLink b("b", "s/env/temp", "display/line");
    TEST_ASSERT_FALSE(b.setup(EbusLink::CONTROLLER));
    // A controller link registers nothing in the settable table.
    EbusLink c("c", "s/env/temp", "d/display/line");
    TEST_ASSERT_TRUE(c.setup(EbusLink::CONTROLLER));
    TEST_ASSERT_FALSE(settable_is_registered("homie/5/s/env/temp"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_local_sources_drive_a_local_target);
    RUN_TEST(test_target_is_written_only_when_the_text_changes);
    RUN_TEST(test_local_retraction_keeps_the_last_good_value);
    RUN_TEST(test_remote_source_arrives_through_settable_dispatch);
    RUN_TEST(test_remote_target_gets_a_non_retained_set);
    RUN_TEST(test_failed_delivery_backs_off);
    RUN_TEST(test_one_remote_topic_feeds_one_link_source);
    RUN_TEST(test_full_settable_table_disables_the_link);
    RUN_TEST(test_device_mode_refuses_patterns_and_bad_references);
    RUN_TEST(test_missing_local_source_is_reported_once);
    RUN_TEST(test_controller_mode_needs_the_opt_in);   // first: before any opt-in
    RUN_TEST(test_controller_link_binds_patterns_once_discovery_settles);
    RUN_TEST(test_controller_pattern_matching_two_nodes_is_refused);
    RUN_TEST(test_controller_target_pattern_needs_a_settable_property);
    RUN_TEST(test_controller_retraction_does_not_reach_the_target);
    RUN_TEST(test_controller_holds_until_ready_and_resends_on_return);
    RUN_TEST(test_controller_links_name_a_device_at_both_ends);
    return UNITY_END();
}
