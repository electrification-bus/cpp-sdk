#pragma once
// The TXT record of each service an eBus entity advertises, built from one description of
// the entity, so every port advertises the same keys and values. Keys, order and
// omission rules follow framework.md "Detail: mDNS Discovery" and the Python
// ebus-service-discovery (Identity.ebus_txt(), device_info_txt(), HttpService.txt()).
// No heap: a TxtRecord holds pointers to the caller's strings and to constants, so every
// string passed in must outlive the record.
#include <stddef.h>
#include <stdint.h>

#ifndef EBUS_TXT_MAX_PAIRS
#define EBUS_TXT_MAX_PAIRS 16       // _ebus._tcp with every key set holds 15
#endif
#define EBUS_TXT_STRING_MAX 255     // one "key=value" string: a single length byte
#define EBUS_TXT_TOTAL_ADVISED 1300 // RFC 6763 6.2: keep a whole record under this

struct TxtPair {
  const char* key;
  const char* value;
};

struct TxtRecord {
  uint8_t count;
  TxtPair pairs[EBUS_TXT_MAX_PAIRS];
};

enum TxtResult {
  TXT_OK = 0,
  TXT_MISSING,     // a required input is null or empty
  TXT_INVALID,     // a key that is empty, not printable ASCII or holds '='; a malformed
                   // device_id list; no role or an unknown role or auth bit
  TXT_TOO_LONG,    // "key=value" over EBUS_TXT_STRING_MAX bytes
  TXT_DUPLICATE,   // the key is already in the record (keys compare case-insensitively)
  TXT_FULL         // EBUS_TXT_MAX_PAIRS pairs already
};

void txt_record_clear(TxtRecord* rec);
// Append one pair. The record is unchanged unless the result is TXT_OK.
TxtResult txt_record_add(TxtRecord* rec, const char* key, const char* value);
// The value for key (case-insensitive), or nullptr.
const char* txt_find(const TxtPair* pairs, uint8_t count, const char* key);
// Bytes the record takes on the wire: one length byte plus "key=value" per pair.
size_t txt_record_wire_size(const TxtRecord* rec);

// Roles, advertised in canonical order as a comma-separated list.
enum EbusRole : uint8_t {
  EBUS_ROLE_DEVICE = 1,
  EBUS_ROLE_CONTROLLER = 2,
  EBUS_ROLE_BROKER_HOST = 4
};
// Authentication methods, the same way.
enum EbusAuthMethod : uint8_t {
  EBUS_AUTH_PASSPHRASE = 1,
  EBUS_AUTH_PRECONFIGURED = 2,
  EBUS_AUTH_MTLS = 4
};
// "device,controller" and so on; "" for 0 or an unknown bit.
const char* ebus_roles_string(uint8_t roles);
// "passphrase,preconfigured" and so on; "" for 0 or an unknown bit.
const char* ebus_auth_methods_string(uint8_t methods);

// One entity. nullptr and "" both mean "not set"; an optional key that is not set is
// omitted.
struct EbusIdentity {
  // Required.
  const char* device_id;      // one id, or several joined with commas (one per device
                              // on the host)
  uint8_t roles;              // EbusRole bits
  const char* manufacturer;   // required in _device-info._tcp
  const char* model;          // required in _device-info._tcp
  const char* serial_number;  // required in _device-info._tcp
  const char* ebus_version;   // not set: EBUS_SPEC_VERSION
  // Optional.
  const char* device_type;
  const char* name;
  const char* fw_version;
  const char* register_path;  // the "register" key, e.g. MDNS_VAL_REGISTER
  const char* broker_ca;
  uint8_t auth_methods;       // EbusAuthMethod bits; 0 omits the key
  const char* hw_version;
  const char* os_version;
  const char* mac;            // lowercase hex without colons
  // Compatibility keys, not in framework.md 0.9 (what SPAN panels advertise). Set
  // homie_domain ("homie", or "ebus" with USE_EBUS_TOPIC) to add homie_domain,
  // homie_version and homie_roles (roles restricted to device and controller).
  const char* homie_domain;
  const char* homie_version;  // not set: "5"
};

// _http._tcp / _https._tcp.
struct EbusHttpInfo {
  const char* path;           // not set: MDNS_VAL_HTTP_PATH, "/api/v1"
  const char* version;        // required: the API version (OpenAPI info.version)
  const char* openapi;        // optional: path of the OpenAPI document
};

// Each builder clears rec and fills it; on a result other than TXT_OK rec is left empty.

// _ebus._tcp: txtvers, ebus_version, roles, device_id, then device_type, name,
// manufacturer, model, fw_version, register, broker_ca, auth_methods, then the
// compatibility keys.
TxtResult txt_build_ebus(const EbusIdentity* id, TxtRecord* rec);
// _device-info._tcp: txtvers, manufacturer, model, serial_number, then fw_version,
// hw_version, os_version, mac.
TxtResult txt_build_device_info(const EbusIdentity* id, TxtRecord* rec);
// _http._tcp / _https._tcp: txtvers, path, version, device_id, then device_type, openapi.
TxtResult txt_build_http(const EbusIdentity* id, const EbusHttpInfo* http, TxtRecord* rec);
// _telnet._tcp, the read-only log stream (not in framework.md): txtvers, device_id,
// kind=serial-log.
TxtResult txt_build_log(const EbusIdentity* id, TxtRecord* rec);
