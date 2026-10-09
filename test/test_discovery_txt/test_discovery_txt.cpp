// SPDX-License-Identifier: MIT
// The TXT record builders (ebus/discovery/txt_records.h). Builds against ebus_discovery
// alone. Each expected record lists its pairs in order. "Python" cases repeat a case from
// ebus-service-discovery 0.5.0 (electrification-bus/python-service-discovery), so both
// libraries build the same record from the same inputs; "esp32-sdk" cases repeat what
// esp32-sdk's src/platform/network.cpp (mdns_register_services(), at 66abe5a) advertises.
#include <unity.h>
#include <ebus/discovery/txt_records.h>
#include <ebus/discovery/mdns_strings.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static TxtRecord rec;

// expected: key, value, key, value, ..., nullptr
static void assert_record(const char* const* expected) {
    uint8_t n = 0;
    for (; expected[2 * n] != nullptr; n++) {
        TEST_ASSERT_TRUE_MESSAGE(n < rec.count, expected[2 * n]);
        TEST_ASSERT_EQUAL_STRING(expected[2 * n], rec.pairs[n].key);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected[2 * n + 1], rec.pairs[n].value,
                                         expected[2 * n]);
    }
    TEST_ASSERT_EQUAL_UINT8(n, rec.count);
}

// Python tests/test_ebus.py:36 (_identity): the base every Python case starts from.
static EbusIdentity python_identity(void) {
    EbusIdentity id = {};
    id.device_id = "dev-1";
    id.roles = EBUS_ROLE_DEVICE;
    id.manufacturer = "Example";
    id.model = "EX-1";
    id.serial_number = "sn-0001";
    return id;
}

// --- Python parity -------------------------------------------------------------------

// Python tests/test_ebus.py:206 (test_identity_ebus_txt_required_and_recommended).
// extra_ebus_txt there adds homie_version and a txtvers that setdefault ignores; here the
// caller appends with txt_record_add(), which refuses the duplicate.
static void test_python_ebus_txt(void) {
    EbusIdentity id = python_identity();
    id.device_id = "dev-1,dev-2";
    id.roles = EBUS_ROLE_DEVICE | EBUS_ROLE_CONTROLLER;
    id.device_type = "example-type";
    id.name = "Example Device";
    id.fw_version = "1.2.3";
    id.register_path = "/api/v1/auth/register";
    id.auth_methods = EBUS_AUTH_PASSPHRASE | EBUS_AUTH_PRECONFIGURED;
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_ebus(&id, &rec));
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_record_add(&rec, "homie_version", "5"));
    TEST_ASSERT_EQUAL_INT(TXT_DUPLICATE, txt_record_add(&rec, "txtvers", "ignored"));
    const char* const expected[] = {
        "txtvers", "1",
        "ebus_version", "0.9",
        "roles", "device,controller",
        "device_id", "dev-1,dev-2",
        "device_type", "example-type",
        "name", "Example Device",
        "manufacturer", "Example",
        "model", "EX-1",
        "fw_version", "1.2.3",
        "register", "/api/v1/auth/register",
        "auth_methods", "passphrase,preconfigured",
        "homie_version", "5",
        nullptr};
    assert_record(expected);
}

// Python tests/test_ebus.py:235 (test_identity_device_info_txt): an empty hw_version is
// omitted.
static void test_python_device_info_txt(void) {
    EbusIdentity id = python_identity();
    id.fw_version = "1.0";
    id.mac = "a0b1c2d3e4f5";
    id.hw_version = "";
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_device_info(&id, &rec));
    const char* const expected[] = {
        "txtvers", "1",
        "manufacturer", "Example",
        "model", "EX-1",
        "serial_number", "sn-0001",
        "fw_version", "1.0",
        "mac", "a0b1c2d3e4f5",
        nullptr};
    assert_record(expected);
}

// Python tests/test_ebus.py:279 (test_http_service_txt). HttpService's defaults
// (src/ebus_service_discovery/ebus.py:383-384) are path "/api/v1" and version "1.0"; here
// the path has the same default and the version is passed.
static void test_python_http_txt(void) {
    EbusIdentity id = python_identity();
    id.device_type = "example-type";
    EbusHttpInfo http = {nullptr, "1.0", "/api/v1/openapi.yml"};
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_http(&id, &http, &rec));
    const char* const expected[] = {
        "txtvers", "1",
        "path", "/api/v1",
        "version", "1.0",
        "device_id", "dev-1",
        "device_type", "example-type",
        "openapi", "/api/v1/openapi.yml",
        nullptr};
    assert_record(expected);
}

// Python tests/test_ebus.py:199 (test_txt_wire_size).
static void test_python_wire_size(void) {
    txt_record_clear(&rec);
    txt_record_add(&rec, "a", "b");
    txt_record_add(&rec, "cd", "");
    TEST_ASSERT_EQUAL_size_t((1 + 3) + (1 + 3), txt_record_wire_size(&rec));
}

// Python tests/test_ebus.py:178 (test_check_txt_rejects_unencodable): "k=" plus 254
// bytes is 256, one over the limit; an empty key and one holding '=' are invalid.
static void test_python_rejects_unencodable(void) {
    static char v254[255];
    memset(v254, 'v', 254);
    v254[254] = '\0';
    txt_record_clear(&rec);
    TEST_ASSERT_EQUAL_INT(TXT_TOO_LONG, txt_record_add(&rec, "k", v254));
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_record_add(&rec, "k", v254 + 1));   // 255: fits
    TEST_ASSERT_EQUAL_INT(TXT_INVALID, txt_record_add(&rec, "a=b", "v"));
    TEST_ASSERT_EQUAL_INT(TXT_INVALID, txt_record_add(&rec, "", "v"));
    TEST_ASSERT_EQUAL_UINT8(1, rec.count);
}

// --- esp32-sdk parity ----------------------------------------------------------------

// What esp32-sdk passes: example device.yml values (doc/MDNS.md), the mdns_strings.h constants,
// HOMIE_TOPIC_DOMAIN "ebus" (-DUSE_EBUS_TOPIC) and HOMIE_VERSION_NUM "5".
static EbusIdentity esp32_identity(void) {
    EbusIdentity id = {};
    id.device_id = "b0b21c90f570";
    id.roles = EBUS_ROLE_DEVICE;
    id.manufacturer = "NESL";
    id.model = "ESP32-POE-ISO";
    id.serial_number = "b0b21c90f570";   // device.serial_number empty: the device id
    id.device_type = "energy.ebus.device.meter";
    id.name = "Lab Meter";
    id.fw_version = "1.4.0";
    id.register_path = MDNS_VAL_REGISTER;
    id.auth_methods = EBUS_AUTH_PASSPHRASE | EBUS_AUTH_PRECONFIGURED;
    id.os_version = "arduino-esp32 3.3.11 / ESP-IDF 5.5.5";
    id.mac = "b0b21c90f573";
    id.homie_domain = "ebus";
    id.homie_version = "5";
    return id;
}

// network.cpp:411-427.
static void test_esp32_ebus_txt(void) {
    EbusIdentity id = esp32_identity();
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_ebus(&id, &rec));
    const char* const expected[] = {
        "txtvers", "1",
        "ebus_version", "0.9",
        "roles", "device",
        "device_id", "b0b21c90f570",
        "device_type", "energy.ebus.device.meter",
        "name", "Lab Meter",
        "manufacturer", "NESL",
        "model", "ESP32-POE-ISO",
        "fw_version", "1.4.0",
        "register", "/api/v1/auth/register",
        "auth_methods", "passphrase,preconfigured",
        "homie_domain", "ebus",
        "homie_version", "5",
        "homie_roles", "device",
        nullptr};
    assert_record(expected);
    TEST_ASSERT_EQUAL_STRING(MDNS_VAL_AUTH_METHODS, txt_find(rec.pairs, rec.count, "auth_methods"));
}

// network.cpp:411-412: homie_role "controller" advertises controller in both keys.
static void test_esp32_controller_roles(void) {
    EbusIdentity id = esp32_identity();
    id.roles = EBUS_ROLE_CONTROLLER;
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_ebus(&id, &rec));
    TEST_ASSERT_EQUAL_STRING("controller", txt_find(rec.pairs, rec.count, "roles"));
    TEST_ASSERT_EQUAL_STRING("controller", txt_find(rec.pairs, rec.count, "homie_roles"));
}

// network.cpp:374-381: no hw_version.
static void test_esp32_device_info_txt(void) {
    EbusIdentity id = esp32_identity();
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_device_info(&id, &rec));
    const char* const expected[] = {
        "txtvers", "1",
        "manufacturer", "NESL",
        "model", "ESP32-POE-ISO",
        "serial_number", "b0b21c90f570",
        "fw_version", "1.4.0",
        "os_version", "arduino-esp32 3.3.11 / ESP-IDF 5.5.5",
        "mac", "b0b21c90f573",
        nullptr};
    assert_record(expected);
}

// network.cpp:387-393.
static void test_esp32_http_txt(void) {
    EbusIdentity id = esp32_identity();
    EbusHttpInfo http = {MDNS_VAL_HTTP_PATH, MDNS_VAL_HTTP_VERSION, MDNS_VAL_HTTP_OPENAPI};
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_http(&id, &http, &rec));
    const char* const expected[] = {
        "txtvers", "1",
        "path", "/api/v1",
        "version", "1.3.0",
        "device_id", "b0b21c90f570",
        "device_type", "energy.ebus.device.meter",
        "openapi", "/api/v1/openapi.yml",
        nullptr};
    assert_record(expected);
}

// network.cpp:401-404.
static void test_esp32_log_txt(void) {
    EbusIdentity id = esp32_identity();
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_log(&id, &rec));
    const char* const expected[] = {
        "txtvers", "1",
        "device_id", "b0b21c90f570",
        "kind", "serial-log",
        nullptr};
    assert_record(expected);
}

// network.cpp:359-362: an empty recommended value is omitted (device_type, name,
// fw_version there; every optional key here).
static void test_unset_optional_keys_are_omitted(void) {
    EbusIdentity id = {};
    id.device_id = "dev-1";
    id.roles = EBUS_ROLE_DEVICE;
    id.name = "";
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_ebus(&id, &rec));
    const char* const expected[] = {
        "txtvers", "1",
        "ebus_version", "0.9",
        "roles", "device",
        "device_id", "dev-1",
        nullptr};
    assert_record(expected);
}

// --- roles, auth methods, compatibility keys -----------------------------------------

static void test_role_and_auth_strings(void) {
    TEST_ASSERT_EQUAL_STRING("device,broker-host",
                             ebus_roles_string(EBUS_ROLE_DEVICE | EBUS_ROLE_BROKER_HOST));
    TEST_ASSERT_EQUAL_STRING("device,controller,broker-host", ebus_roles_string(7));
    TEST_ASSERT_EQUAL_STRING("", ebus_roles_string(0));
    TEST_ASSERT_EQUAL_STRING("", ebus_roles_string(8));
    TEST_ASSERT_EQUAL_STRING("passphrase,mtls",
                             ebus_auth_methods_string(EBUS_AUTH_PASSPHRASE | EBUS_AUTH_MTLS));
    TEST_ASSERT_EQUAL_STRING("", ebus_auth_methods_string(8));
}

// homie_roles carries only the Homie roles; homie_version defaults to 5.
static void test_homie_roles_leave_out_broker_host(void) {
    EbusIdentity id = python_identity();
    id.roles = EBUS_ROLE_DEVICE | EBUS_ROLE_BROKER_HOST;
    id.homie_domain = "homie";
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_ebus(&id, &rec));
    TEST_ASSERT_EQUAL_STRING("device,broker-host", txt_find(rec.pairs, rec.count, "roles"));
    TEST_ASSERT_EQUAL_STRING("homie", txt_find(rec.pairs, rec.count, "homie_domain"));
    TEST_ASSERT_EQUAL_STRING("5", txt_find(rec.pairs, rec.count, "homie_version"));
    TEST_ASSERT_EQUAL_STRING("device", txt_find(rec.pairs, rec.count, "homie_roles"));

    id.roles = EBUS_ROLE_BROKER_HOST;
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_ebus(&id, &rec));
    TEST_ASSERT_NULL(txt_find(rec.pairs, rec.count, "homie_roles"));
}

// --- refusals ------------------------------------------------------------------------

// Python tests/test_ebus.py:247-262 (test_identity_validation) refuses the same inputs.
static void test_ebus_refusals_leave_the_record_empty(void) {
    EbusIdentity id = python_identity();
    TEST_ASSERT_EQUAL_INT(TXT_OK, txt_build_ebus(&id, &rec));
    id.device_id = "";
    TEST_ASSERT_EQUAL_INT(TXT_MISSING, txt_build_ebus(&id, &rec));
    TEST_ASSERT_EQUAL_UINT8(0, rec.count);
    id.device_id = "a,,b";
    TEST_ASSERT_EQUAL_INT(TXT_INVALID, txt_build_ebus(&id, &rec));
    id.device_id = "a,";
    TEST_ASSERT_EQUAL_INT(TXT_INVALID, txt_build_ebus(&id, &rec));
    id.device_id = "dev-1";
    id.roles = 0;
    TEST_ASSERT_EQUAL_INT(TXT_INVALID, txt_build_ebus(&id, &rec));
    id.roles = 8;
    TEST_ASSERT_EQUAL_INT(TXT_INVALID, txt_build_ebus(&id, &rec));
    id.roles = EBUS_ROLE_DEVICE;
    id.auth_methods = 8;
    TEST_ASSERT_EQUAL_INT(TXT_INVALID, txt_build_ebus(&id, &rec));
    TEST_ASSERT_EQUAL_UINT8(0, rec.count);
}

static void test_device_info_and_http_refusals(void) {
    EbusIdentity id = python_identity();
    id.serial_number = nullptr;
    TEST_ASSERT_EQUAL_INT(TXT_MISSING, txt_build_device_info(&id, &rec));
    TEST_ASSERT_EQUAL_UINT8(0, rec.count);
    id = python_identity();
    EbusHttpInfo http = {nullptr, nullptr, nullptr};
    TEST_ASSERT_EQUAL_INT(TXT_MISSING, txt_build_http(&id, &http, &rec));
    id.device_id = nullptr;
    TEST_ASSERT_EQUAL_INT(TXT_MISSING, txt_build_log(&id, &rec));
}

// A value that makes "key=value" too long fails the build, which leaves nothing behind.
static void test_too_long_value_fails_the_build(void) {
    static char name[260];
    memset(name, 'n', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    EbusIdentity id = python_identity();
    id.name = name;
    TEST_ASSERT_EQUAL_INT(TXT_TOO_LONG, txt_build_ebus(&id, &rec));
    TEST_ASSERT_EQUAL_UINT8(0, rec.count);
}

static void test_add_until_full_and_case_insensitive_keys(void) {
    static char keys[EBUS_TXT_MAX_PAIRS + 1][4];
    txt_record_clear(&rec);
    for (int i = 0; i <= EBUS_TXT_MAX_PAIRS; i++) {
        keys[i][0] = 'k';
        keys[i][1] = (char)('a' + i / 10);
        keys[i][2] = (char)('0' + i % 10);
        keys[i][3] = '\0';
        TxtResult r = txt_record_add(&rec, keys[i], "v");
        TEST_ASSERT_EQUAL_INT(i < EBUS_TXT_MAX_PAIRS ? TXT_OK : TXT_FULL, r);
    }
    TEST_ASSERT_EQUAL_STRING("v", txt_find(rec.pairs, rec.count, "KA0"));
    txt_record_clear(&rec);
    txt_record_add(&rec, "Name", "x");
    TEST_ASSERT_EQUAL_INT(TXT_DUPLICATE, txt_record_add(&rec, "name", "y"));
    TEST_ASSERT_NULL(txt_find(rec.pairs, rec.count, "nam"));
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_python_ebus_txt);
    RUN_TEST(test_python_device_info_txt);
    RUN_TEST(test_python_http_txt);
    RUN_TEST(test_python_wire_size);
    RUN_TEST(test_python_rejects_unencodable);

    RUN_TEST(test_esp32_ebus_txt);
    RUN_TEST(test_esp32_controller_roles);
    RUN_TEST(test_esp32_device_info_txt);
    RUN_TEST(test_esp32_http_txt);
    RUN_TEST(test_esp32_log_txt);
    RUN_TEST(test_unset_optional_keys_are_omitted);

    RUN_TEST(test_role_and_auth_strings);
    RUN_TEST(test_homie_roles_leave_out_broker_host);

    RUN_TEST(test_ebus_refusals_leave_the_record_empty);
    RUN_TEST(test_device_info_and_http_refusals);
    RUN_TEST(test_too_long_value_fails_the_build);
    RUN_TEST(test_add_until_full_and_case_insensitive_keys);

    return UNITY_END();
}
