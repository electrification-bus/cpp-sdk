# ebus_discovery

The eBus mDNS contract (framework.md, "Detail: mDNS Discovery") as code: the broker service types and the order a querier browses them in, the TXT record of each service an entity advertises, and the interface a port implements over its mDNS stack. It depends on the C and C++ standard libraries only, and allocates nothing. It is the C++ counterpart of the contract module of the Python [ebus-service-discovery](https://github.com/electrification-bus/python-service-discovery) (`ebus.py`). Sources are in `discovery/`; CMake builds them as the `ebus_discovery` target, which `ebus_core` links.

| Header | Contents |
|---|---|
| `ebus/discovery/txt_records.h` | `EbusIdentity`, `TxtRecord`, and the builders for `_ebus._tcp`, `_device-info._tcp`, `_http._tcp` and `_telnet._tcp` |
| `ebus/discovery/mdns_backend.h` | `MdnsBackend`, the interface a port implements: advertise, browse, resolve |
| `ebus/discovery/broker_browse.h` | `broker_browse()` and `broker_reresolve()`, broker discovery over an `MdnsBackend` |
| `ebus/discovery/broker_discovery.h` | Broker service names, DNS-SD types and default ports; the browse list (esp32-sdk's `mqtt.discover`); host name trimming; the re-resolve count |
| `ebus/discovery/mdns_strings.h` | Service types, TXT keys and fixed values |

`<platform/broker_discovery.h>` and `<platform/mdns_strings.h>`, the earlier paths of two of them, forward to these headers.

## TXT records

Describe the entity once, then build each service's record and hand it to the backend:

```cpp
EbusIdentity id = {};
id.device_id = "b0b21c90f570";
id.roles = EBUS_ROLE_DEVICE;
id.manufacturer = "NESL";
id.model = "ESP32-POE-ISO";
id.serial_number = "b0b21c90f570";
id.register_path = MDNS_VAL_REGISTER;
id.auth_methods = EBUS_AUTH_PASSPHRASE | EBUS_AUTH_PRECONFIGURED;

TxtRecord rec;
if (txt_build_ebus(&id, &rec) == TXT_OK) {
    backend.advertise(MDNS_TYPE_EBUS, mqtt_port, rec.pairs, rec.count);
}
```

A `TxtRecord` holds up to `EBUS_TXT_MAX_PAIRS` (16) key and value pointers: to the caller's strings and to constants. Every string in the `EbusIdentity` must therefore outlive the record; the backend copies the pairs when it advertises.

| Service | Builder | Keys, in order |
|---|---|---|
| `_ebus._tcp` | `txt_build_ebus()` | `txtvers`, `ebus_version`, `roles`, `device_id`; then, when set, `device_type`, `name`, `manufacturer`, `model`, `fw_version`, `register`, `broker_ca`, `auth_methods`; then, when `homie_domain` is set, `homie_domain`, `homie_version`, `homie_roles` |
| `_device-info._tcp` | `txt_build_device_info()` | `txtvers`, `manufacturer`, `model`, `serial_number`; then, when set, `fw_version`, `hw_version`, `os_version`, `mac` |
| `_http._tcp`, `_https._tcp` | `txt_build_http()` | `txtvers`, `path`, `version`, `device_id`; then, when set, `device_type`, `openapi` |
| `_telnet._tcp` | `txt_build_log()` | `txtvers`, `device_id`, `kind` (`serial-log`) |

The rules match the Python library's `Identity` and `HttpService`:

- `nullptr` and `""` both mean "not set", and a key that is not set is omitted.
- `device_id` is one id, or several joined with commas for a host that carries several devices; an empty element is refused.
- `roles` and `auth_methods` are bit sets (`EbusRole`, `EbusAuthMethod`), written in the order framework.md lists the values: `device,controller,broker-host` and `passphrase,preconfigured,mtls`. At least one role is required.
- `ebus_version` defaults to `EBUS_SPEC_VERSION` (`0.9`), `path` to `/api/v1`, `homie_version` to `5`. The HTTP `version` (the API's OpenAPI `info.version`) is required.
- A `key=value` string over 255 bytes (`TXT_TOO_LONG`), a key that is empty, not printable ASCII or holds `=` (`TXT_INVALID`), and a missing required value (`TXT_MISSING`) fail the build, which leaves the record empty.

`homie_domain`, `homie_version` and `homie_roles` are not in framework.md 0.9; they are the keys SPAN panels advertise, and esp32-sdk advertises them too. `homie_roles` is `roles` restricted to `device` and `controller`.

`txt_record_add()` appends a key the builders do not know, after a build; it refuses a key already in the record (keys compare case-insensitively, RFC 6763 6.4) and a full record. `txt_record_wire_size()` gives the bytes a record takes on the wire; RFC 6763 6.2 advises keeping it under 1300 (`EBUS_TXT_TOTAL_ADVISED`). `txt_find()` looks a key up in a record or in a browsed instance's pairs.

### Across implementations

`test/test_discovery_txt` builds the records of the Python library's TXT tests and those esp32-sdk's `network.cpp` advertises, and expects the same pairs in the same order. Remaining differences:

| Item | esp32-sdk | ebus-service-discovery | framework.md |
|---|---|---|---|
| `_ebus._tcp` port | `mqtt.port` | the first HTTP port, else 0 | not specified |
| `homie_*` keys | advertised | not advertised (an `extra_ebus_txt` can add them) | not defined |
| HTTP `version` | `1.3.0` | defaults to `1.0` | example `1.0` |
| `hw_version`, `broker_ca` | not advertised | when set | recommended |
| `_telnet._tcp` | advertised while the log stream listens | not defined | not defined ([specification issue 25](https://github.com/electrification-bus/specification/issues/25)) |

## The backend

```cpp
struct MdnsInstance {
    const char* instance;   // instance name, or ""
    const char* host;       // SRV target as received
    const char* address;    // resolved address as text, or ""
    uint16_t port;          // SRV port, or 0
    const TxtPair* txt;
    uint8_t txt_count;
};
typedef bool (*mdns_instance_fn)(void* ctx, const MdnsInstance* instance);

class MdnsBackend {
 public:
    virtual bool advertise(const char* service_type, uint16_t port, const TxtPair* txt, uint8_t txt_count) = 0;
    virtual int browse(const char* service_type, uint32_t timeout_ms, mdns_instance_fn fn, void* ctx) = 0;
    virtual bool resolve(const char* hostname, uint32_t timeout_ms, char* address, size_t address_size) = 0;
};
```

Service types are full DNS-SD types (`_ebus._tcp`, `MDNS_TYPE_EBUS`; `broker_service_type()` for the brokers); a backend splits them as its stack needs. `advertise()` registers the service under the host name the port claimed, with that name as the instance name, as framework.md and esp32-sdk do. `browse()` passes each instance to the callback until it returns false; the instance's strings belong to the backend and last only for the call, so nothing is allocated on either side. `resolve()` takes the bare host name, without `.local`. Claiming the host name is part of each stack's own setup (`MDNS.begin()` on ESP32), so a port does it before using the backend.

## Broker discovery

`broker_browse(backend, list, timeout_ms, &found)` browses each service type of a `BrokerDiscoveryList` in order (the default is `_secure-mqtt._tcp`, then `_mqtt._tcp`) and takes the first instance that has an address. `found` holds the service, the address, the SRV port (the service's default port when the record has none) and the bare host name (empty when the SRV target is empty or does not fit). Repeating a browse that found nothing, and how often, is the caller's, since it needs a clock.

After `BROKER_RERESOLVE_AFTER_FAILURES` consecutive failed reconnects (`broker_reconnect_failed()`), `broker_reresolve(backend, &found, timeout_ms)` sends one host query for `found.hostname`: `BROKER_RERESOLVE_MOVED` with the new address in `found`, `BROKER_RERESOLVE_UNCHANGED`, or `BROKER_RERESOLVE_FAILED`, which leaves `found` as it was. These are esp32-sdk's rules (`mdns_query_one_service()` and `mdns_reresolve_broker()`; its [doc/MDNS.md](https://github.com/electrification-bus/esp32-sdk/blob/main/doc/MDNS.md#broker-discovery)).

## A POSIX backend

The POSIX port has no `MdnsBackend` yet. On a host that runs an mDNS responder (mDNSResponder on macOS, avahi-daemon on most Linux distributions), a backend should advertise through that responder rather than run a second one beside it: the Python library measured that address records published beside mDNSResponder under the host's name cause name conflicts and can make it rename the host for good (`_mdns_core.py`, module docstring). The `dns_sd.h` API (`DNSServiceRegister`, `DNSServiceBrowse`, `DNSServiceResolve`, `DNSServiceGetAddrInfo`) is native on macOS and available on Linux through Avahi's compatibility library, so one backend can cover both; Avahi's own client API is the Linux-only alternative. A host with no responder needs an embedded mDNS library.

## Building

`ebus_discovery` has `discovery/include` as its only include directory. CI (`core-host-build`, step "ebus_discovery alone") compiles each source and header in `discovery/` with that one `-I`, and the `ebus_discovery_header_check` target compiles each header against `ebus_discovery` alone. Its Unity suites are `test/test_discovery_*`, which link `ebus_discovery` without `ebus_core`. A project that wants only this layer can `add_subdirectory(<path>/cpp-sdk/discovery)`.

PlatformIO builds it as part of the single `ebus_core` library: `library.json` compiles `discovery/src/` and adds `discovery/include` to the include path, which PlatformIO also gives the library's dependents.
