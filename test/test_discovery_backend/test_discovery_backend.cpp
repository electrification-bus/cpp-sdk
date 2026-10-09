// SPDX-License-Identifier: MIT
// Broker discovery over the MdnsBackend interface (ebus/discovery/broker_browse.h), and
// advertising a built TXT record through it, against a fake backend. Builds against
// ebus_discovery alone. The selection rules are esp32-sdk's mdns_query_one_service() and
// mdns_reresolve_broker() (src/platform/network.cpp at 66abe5a).
#include <unity.h>
#include <ebus/discovery/broker_browse.h>
#include <ebus/discovery/mdns_strings.h>
#include <ebus/discovery/txt_records.h>
#include <stdio.h>
#include <string.h>

// A LAN in a table: browse() reports the instances whose type matches, in table order.
struct FakeInstance {
    const char* type;
    const char* host;
    const char* address;
    uint16_t port;
};

class FakeMdns : public MdnsBackend {
 public:
    const FakeInstance* lan = nullptr;
    int lan_count = 0;
    int fail_browse_of = -1;            // index into browsed[] whose query fails
    const char* browsed[8];
    int browse_calls = 0;
    int callbacks = 0;
    const char* resolve_answer = nullptr;
    char resolved_name[EBUS_MDNS_HOST_MAX];

    const char* adv_type = nullptr;
    uint16_t adv_port = 0;
    const TxtPair* adv_txt = nullptr;
    uint8_t adv_count = 0;

    bool advertise(const char* service_type, uint16_t port, const TxtPair* txt,
                   uint8_t txt_count) override {
        adv_type = service_type;
        adv_port = port;
        adv_txt = txt;
        adv_count = txt_count;
        return true;
    }

    int browse(const char* service_type, uint32_t, mdns_instance_fn fn, void* ctx) override {
        int call = browse_calls++;
        if (call < 8) browsed[call] = service_type;
        if (call == fail_browse_of) return -1;
        int passed = 0;
        for (int i = 0; i < lan_count; i++) {
            if (strcmp(lan[i].type, service_type) != 0) continue;
            MdnsInstance inst = {"inst", lan[i].host, lan[i].address, lan[i].port, nullptr, 0};
            passed++;
            callbacks++;
            if (!fn(ctx, &inst)) break;
        }
        return passed;
    }

    bool resolve(const char* hostname, uint32_t, char* address, size_t address_size) override {
        snprintf(resolved_name, sizeof(resolved_name), "%s", hostname);
        if (resolve_answer == nullptr) return false;
        snprintf(address, address_size, "%s", resolve_answer);
        return true;
    }
};

static FakeMdns mdns;
static BrokerDiscoveryList list;
static BrokerFound found;

void setUp(void) {
    mdns = FakeMdns();
    broker_discovery_default(&list);
    memset(&found, 0x55, sizeof(found));
}
void tearDown(void) {}

static void use_lan(const FakeInstance* lan, int n) {
    mdns.lan = lan;
    mdns.lan_count = n;
}

static void test_service_types(void) {
    TEST_ASSERT_EQUAL_STRING("_secure-mqtt._tcp", broker_service_type(BROKER_SVC_SECURE_MQTT));
    TEST_ASSERT_EQUAL_STRING("_mqtt._tcp", broker_service_type(BROKER_SVC_MQTT));
    TEST_ASSERT_EQUAL_STRING("_mqtt-ws._tcp", broker_service_type(BROKER_SVC_MQTT_WS));
    TEST_ASSERT_EQUAL_STRING("_mqtt-wss._tcp", broker_service_type(BROKER_SVC_MQTT_WSS));
    TEST_ASSERT_EQUAL_STRING("", broker_service_type(BROKER_SVC_COUNT));
}

// The default list browses secure-mqtt first, and stops at the first usable instance.
static void test_list_order_wins(void) {
    static const FakeInstance lan[] = {
        {"_mqtt._tcp", "plain.local", "192.168.1.20", 1883},
        {"_secure-mqtt._tcp", "tls.local.", "192.168.1.10", 8883},
        {"_secure-mqtt._tcp", "tls2.local", "192.168.1.11", 8883},
    };
    use_lan(lan, 3);
    TEST_ASSERT_TRUE(broker_browse(&mdns, &list, 3000, &found));
    TEST_ASSERT_EQUAL_UINT8(BROKER_SVC_SECURE_MQTT, found.service);
    TEST_ASSERT_EQUAL_STRING("192.168.1.10", found.address);
    TEST_ASSERT_EQUAL_STRING("tls", found.hostname);
    TEST_ASSERT_EQUAL_UINT16(8883, found.port);
    TEST_ASSERT_EQUAL_INT(1, mdns.browse_calls);
    TEST_ASSERT_EQUAL_INT(1, mdns.callbacks);

    broker_discovery_clear(&list);
    broker_discovery_add(&list, "mqtt");
    broker_discovery_add(&list, "secure-mqtt");
    TEST_ASSERT_TRUE(broker_browse(&mdns, &list, 3000, &found));
    TEST_ASSERT_EQUAL_UINT8(BROKER_SVC_MQTT, found.service);
    TEST_ASSERT_EQUAL_STRING("plain", found.hostname);
}

// An instance without an address is skipped (network.cpp:168-171); a missing SRV port
// becomes the service's default (network.cpp:174).
static void test_unresolved_skipped_and_default_port(void) {
    static const FakeInstance lan[] = {
        {"_secure-mqtt._tcp", "a.local", "", 8883},
        {"_secure-mqtt._tcp", "b.local", "fd00::5", 0},
    };
    use_lan(lan, 2);
    TEST_ASSERT_TRUE(broker_browse(&mdns, &list, 3000, &found));
    TEST_ASSERT_EQUAL_STRING("fd00::5", found.address);
    TEST_ASSERT_EQUAL_STRING("b", found.hostname);
    TEST_ASSERT_EQUAL_UINT16(8883, found.port);
    TEST_ASSERT_EQUAL_INT(2, mdns.callbacks);
}

// A secure-mqtt query that fails moves the browse on to mqtt.
static void test_failed_query_moves_to_next_type(void) {
    static const FakeInstance lan[] = {
        {"_secure-mqtt._tcp", "tls.local", "192.168.1.10", 8883},
        {"_mqtt._tcp", "plain.local", "192.168.1.20", 0},
    };
    use_lan(lan, 2);
    mdns.fail_browse_of = 0;
    TEST_ASSERT_TRUE(broker_browse(&mdns, &list, 3000, &found));
    TEST_ASSERT_EQUAL_STRING("_secure-mqtt._tcp", mdns.browsed[0]);
    TEST_ASSERT_EQUAL_STRING("_mqtt._tcp", mdns.browsed[1]);
    TEST_ASSERT_EQUAL_UINT8(BROKER_SVC_MQTT, found.service);
    TEST_ASSERT_EQUAL_UINT16(1883, found.port);
}

// A host name that does not fit is dropped (network.cpp:175-178), so the broker is used
// but never re-resolved.
static void test_unusable_hostname_still_found(void) {
    static char longhost[EBUS_MDNS_HOST_MAX + 8];
    memset(longhost, 'h', sizeof(longhost) - 1);
    longhost[sizeof(longhost) - 1] = '\0';
    static FakeInstance lan[] = {{"_mqtt._tcp", nullptr, "10.0.0.2", 1883}};
    lan[0].host = longhost;
    use_lan(lan, 1);
    TEST_ASSERT_TRUE(broker_browse(&mdns, &list, 3000, &found));
    TEST_ASSERT_EQUAL_STRING("", found.hostname);
    TEST_ASSERT_EQUAL_INT(BROKER_RERESOLVE_FAILED, broker_reresolve(&mdns, &found, 2000));
}

static void test_nothing_found_clears_out(void) {
    static const FakeInstance lan[] = {
        {"_mqtt-ws._tcp", "ws.local", "10.0.0.3", 9001},   // not in the list
        {"_mqtt._tcp", "x.local", "", 1883},
    };
    use_lan(lan, 2);
    TEST_ASSERT_FALSE(broker_browse(&mdns, &list, 3000, &found));
    TEST_ASSERT_EQUAL_STRING("", found.address);
    TEST_ASSERT_EQUAL_STRING("", found.hostname);
    TEST_ASSERT_EQUAL_UINT16(0, found.port);
    TEST_ASSERT_EQUAL_INT(2, mdns.browse_calls);
}

// network.cpp:216-240: one host query; the address moves only when the answer differs.
static void test_reresolve(void) {
    static const FakeInstance lan[] = {{"_secure-mqtt._tcp", "tls.local", "192.168.1.10", 8883}};
    use_lan(lan, 1);
    TEST_ASSERT_TRUE(broker_browse(&mdns, &list, 3000, &found));

    mdns.resolve_answer = "192.168.1.10";
    TEST_ASSERT_EQUAL_INT(BROKER_RERESOLVE_UNCHANGED, broker_reresolve(&mdns, &found, 2000));
    TEST_ASSERT_EQUAL_STRING("tls", mdns.resolved_name);

    mdns.resolve_answer = "192.168.1.44";
    TEST_ASSERT_EQUAL_INT(BROKER_RERESOLVE_MOVED, broker_reresolve(&mdns, &found, 2000));
    TEST_ASSERT_EQUAL_STRING("192.168.1.44", found.address);
    TEST_ASSERT_EQUAL_UINT16(8883, found.port);

    mdns.resolve_answer = nullptr;
    TEST_ASSERT_EQUAL_INT(BROKER_RERESOLVE_FAILED, broker_reresolve(&mdns, &found, 2000));
    TEST_ASSERT_EQUAL_STRING("192.168.1.44", found.address);
}

// A built record goes to the backend as it is.
static void test_advertise_a_built_record(void) {
    EbusIdentity id = {};
    id.device_id = "dev-1";
    id.roles = EBUS_ROLE_DEVICE;
    TxtRecord rec;
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_ebus(&id, &rec));
    TEST_ASSERT_TRUE(mdns.advertise(MDNS_TYPE_EBUS, 1883, rec.pairs, rec.count));
    TEST_ASSERT_EQUAL_STRING("_ebus._tcp", mdns.adv_type);
    TEST_ASSERT_EQUAL_UINT16(1883, mdns.adv_port);
    TEST_ASSERT_EQUAL_UINT8(4, mdns.adv_count);
    TEST_ASSERT_EQUAL_STRING("dev-1", txt_find(mdns.adv_txt, mdns.adv_count, "device_id"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_service_types);
    RUN_TEST(test_list_order_wins);
    RUN_TEST(test_unresolved_skipped_and_default_port);
    RUN_TEST(test_failed_query_moves_to_next_type);
    RUN_TEST(test_unusable_hostname_still_found);
    RUN_TEST(test_nothing_found_clears_out);
    RUN_TEST(test_reresolve);
    RUN_TEST(test_advertise_a_built_record);
    return UNITY_END();
}
