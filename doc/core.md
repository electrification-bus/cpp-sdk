# ebus_core

The portable part of the eBus / Homie 5 SDK: the Homie device model, `/set` dispatch and the controller, depending on the C and C++ standard libraries and ArduinoJson, with no Arduino, ESP-IDF or FreeRTOS header. It targets other MCUs and operating systems too (Zephyr, FreeRTOS on STM32 or NXP, embedded Linux). A port supplies the MQTT client, the console and the clock through the interfaces below.

| Header | Contents |
|---|---|
| `homie/Device.h`, `homie/Node.h`, `homie/Property.h` | The Homie object model: device tree, `$state` / `$description` lifecycle, property values and the publish-on-change gate |
| `homie/homie_settable.h` | The settable table: `/set` registration, dispatch, `deliver_local_set()` |
| `homie/controller.h` | Controller role: discovery, device tree from `$description`, `controller_set_property()` |
| `homie/homie_transport.h` | `HomieTransport`, the MQTT interface a port implements: `ebus_mqtt`'s `MqttTransport` ([`mqtt.md`](mqtt.md)) plus the Property `queue_publish()` adapter; `MAX_DATA_LEN`; `mqtt_qos` and `homie_qos()` |
| `homie/homie_log.h` | `homie_logf()` / `homie_logln()` and the sink a port binds |
| `homie/homie_clock.h` | `homie_now_ms()` / `homie_sleep_ms()` and the hooks a port binds |
| `homie/homie_json.h`, `util/jsonUtils.h` | `$description` (de)serialization helpers (ArduinoJson) |
| `homie/homie.h` | Homie version, topic domain and prefix (`USE_EBUS_TOPIC` at build time, `homie_set_topic_domain()` at run time), datatype and attribute strings |
| `homie/homie_limits.h` | Longest ids and topics; every buffer is sized from these. esp32-sdk's `./ebus-esp32 generate` reads this file to check `device.yml` |
| `homie/homie_enums.h` | `PropertyDatatype` and `Unit` enums and their Homie strings |
| `homie/homie_descriptor.h` | `PropertyDesc`, the declarative property descriptor |
| `homie/homie_id.h` | Id sanitizing, MAC-based ids, id templates, child ids |
| `homie/homie_datatype.h` | Payload validation and parsing for every Homie datatype |
| `homie/controller_inbox.h` | Controller message inbox (caller-owned arena) and topic parser |
| `homie/ebus_vocabulary.h` | Registered eBus device and capability type strings |
| `platform/broker_discovery.h`, `platform/mdns_strings.h` | Forward to `ebus_discovery`'s `ebus/discovery/broker_discovery.h` and `ebus/discovery/mdns_strings.h` ([`discovery.md`](discovery.md)) |

## What a port implements

| Piece | Contract | ESP32 port ([esp32-sdk](https://github.com/electrification-bus/esp32-sdk)) |
|---|---|---|
| MQTT transport | A `HomieTransport` subclass. `publish()` and `subscribe()` run on the task that owns the client and never inside its receive callback. `queue_publish()` may be called from any task; the port overrides either the generic overload, which must call `done(ctx, payload, length, sent)` exactly once per call when `done` is set, or the Property overload, which must call `source->queued_publish_done(payload, length, sent)` exactly once per call when `source` is set, in both cases including when it fails ([`mqtt.md`](mqtt.md)). `last_error()` is for logs only. | `MqttClientTransport` in `include/platform/mqtt_client.h`, over arduino-mqtt and the FreeRTOS publish queue; the instance is `mqtt_transport` |
| Clock | `homie_clock_bind(now_ms, sleep_ms)`. Required: unbound, the tick reads 0 and the sleep returns at once. | `millis()` / `delay()`, bound in `src/platform/homie_port.cpp` |
| Console | `homie_log_bind(vprintf_sink)`. Optional: unbound, output goes to `vprintf()` on stdout. | The UART + TCP tee, bound in `src/platform/homie_port.cpp` |
| Settable table | Storage for `capacity` entries, passed with the transport and the driver call to `settable_table_bind()` before anything registers. The receive callback asks `settable_is_registered(topic)` and, if so, copies the message for the owning task, which runs `settable_dispatch(topic, value)`. After every connect, subscribe `settable_topic(i)` for `i < settable_count()` (step 2 of the reconnect order in [`mqtt.md`](mqtt.md)). | `mqtt_queue_init()`, `subscriber_callback()`, `mqtt_process_queue()` and `broker_connect()` in `src/platform/mqtt_client.cpp` |
| Driver call | A `settable_entity_call_t` that calls a driver's settable callback, defined where `NodeEntity` is. | `node_entity_settable_call()` in `src/node/NodeEntity.cpp` |
| Root device | `Device::init(name, id, type, &transport)`; child devices share the root's `mqttClient()`. | `theDevice.init(..., &mqtt_transport)` in `src/main.cpp` |
| QoS | Set `mqtt_qos` (default 2) for retained publications, if it is configurable. | `src/platform/config.cpp`, from the `mqtt_qos` config key |

The controller needs `controller_init(&transport, ...)` and the receive callback passing every message the settable table does not claim to `controller_mqtt_callback()`.

The POSIX port in [`ports/posix/`](../ports/posix/) implements the same pieces over Eclipse Paho MQTT C; its README maps each one.

## Rules for code in the core

- Include only standard headers, ArduinoJson and other core headers; `ebus_mqtt` (`mqtt/`) and `ebus_discovery` (`discovery/`) each include only standard headers and their own. CI enforces all three.
- No Arduino `String`, no STL containers, no lambdas: callers pass buffers.
- No new dynamic allocation. What allocates today predates the move: `Device::addNode()` (each `Node`), `Device::addNodePropertiesFromConfigJson()` (each `Property`, controller path only), the `$description` buffer (`MAX_DATA_LEN` bytes, once, on first use), the controller's `Device` per discovered device, and ArduinoJson's `JsonDocument` pools.
- C++17, GCC or Clang (`__atomic` builtins, `__attribute__((format))`).

## Building

**PlatformIO**: `library.json` makes this repository one library that declares its ArduinoJson dependency, compiles only `src/`, `mqtt/src/` and `discovery/src/`, and puts `include/`, `mqtt/include/` and `discovery/include/` on the include path of the library and of the project that uses it; a project adds it with a `lib_deps` git URL. A library is compiled without the project's `build_src_flags`, so a macro the core reads (`MAX_DATA_LEN`, `CONTROLLER_INBOX_BYTES`, `USE_EBUS_TOPIC`) must be set in `build_flags`.

**CMake**, for any other build system or host:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

A parent project uses `add_subdirectory(<path>/cpp-sdk)` and links the `ebus_core` target, which carries its include directory, C++17, ArduinoJson, `ebus_mqtt` and `ebus_discovery`. A parent that already defines an `ArduinoJson` target keeps it; otherwise `FetchContent` downloads ArduinoJson 7.4.3, the release esp32-sdk's `platformio.ini` uses, checked against its SHA-256, so both builds serialize `$description` with the same code. `find_package()` was not used because few systems package ArduinoJson and none would pin that release. To build offline, pass `-DFETCHCONTENT_SOURCE_DIR_ARDUINOJSON=<checkout>`. `-DEBUS_CORE_EBUS_TOPIC=ON` defines `USE_EBUS_TOPIC`, which moves the topic root from `homie/5` to `ebus/5`; the ESP32 firmware sets it in `platformio.ini`. Built as the top-level project, the targets `ebus_core_header_check`, `ebus_mqtt_header_check` and `ebus_discovery_header_check` also compile each public header in a translation unit of its own (a component's header against that component alone), and the Unity suites in `test/` are built and registered with CTest (`-DEBUS_CORE_TESTS=OFF` skips them).

CI (`core-host-build` in `.github/workflows/ci.yml`) runs that CMake build and the tests with GCC and Clang, warnings as errors, and only the core's include directory and ArduinoJson on the core's path.

## Include paths

The headers keep the paths they had under the project's `include/` (`<homie/Device.h>`, `<util/jsonUtils.h>`, `<platform/broker_discovery.h>`), so no `#include` in esp32-sdk changed when they moved. The two `platform/` headers now forward to `ebus_discovery`. `platform/` and `util/` are the wrong names for a portable library; renaming them (to `ebus/`, say) takes one sweep here and in esp32-sdk.

## Still in esp32-sdk

`NodeEntity` and `NodeProperty` (esp32-sdk's `include/node/`, `src/node/`), the driver contract, stay in the firmware for two reasons. `include/node/NodeEntity.h` includes `platform/log_stream.h`, which is what routes every `lib/` driver's `Serial.printf()` through the UART + TCP tee; moving it needs a replacement for that include that keeps the tee. And both resolve properties against `theDevice` and `g_active_setup_device` (`include/homie/homie_globals.h`), globals the application defines in `src/platform/config.cpp`.

## Next step

- **Driver contract**: move `NodeEntity` and `NodeProperty` into the core once drivers log through `homie_logf()` (or a `Serial` shim that keeps the tee) and the active device is passed to `NodeProperty::setup()` rather than read from a global. `node_entity_settable_call()` then moves with them.
- **Non-blocking subscribe retry**: `Property::subscribe()` still sleeps 250 ms and 500 ms between its three attempts, now through `homie_sleep_ms()`.
