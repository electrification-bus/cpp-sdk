# eBus C++ SDK

[![CI](https://github.com/electrification-bus/cpp-sdk/actions/workflows/ci.yml/badge.svg)](https://github.com/electrification-bus/cpp-sdk/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A portable C++17 core for devices and controllers on the [Electrification Bus](https://github.com/electrification-bus/specification) (eBus), which builds on the [Homie 5 MQTT convention](https://homieiot.github.io/). The core holds the Homie device model (Device, Node, Property, the `$state` and `$description` lifecycle), `/set` dispatch, the controller, id and payload validation, and broker-discovery parsing. It depends on the C and C++ standard libraries and [ArduinoJson](https://arduinojson.org/), with no Arduino, ESP-IDF or FreeRTOS header, and a port supplies the MQTT client, the console and the clock.

[`.ebus-spec.json`](.ebus-spec.json) records the specification commit, `framework.md` version, and framework features this SDK implements.

## Supported targets

| Target | Where | MQTT client |
|---|---|---|
| ESP32 (Arduino framework, PlatformIO) | [esp32-sdk](https://github.com/electrification-bus/esp32-sdk) | arduino-mqtt |
| POSIX (Linux, macOS) | [`ports/posix/`](ports/posix/) in this repository | Eclipse Paho MQTT C |

## Status

Version 0.2.0, alpha. Known limits:

- The POSIX demo build has no TLS; it connects to plain-TCP brokers only.
- After a broker loses its retained messages, a reconnect re-asserts `$state` but not `$description` (see the POSIX port's [Known gaps](ports/posix/README.md#known-gaps); esp32-sdk has the same gap).
- The driver contract (`NodeEntity`, `NodeProperty`) is still in esp32-sdk ([why](doc/core.md#still-in-esp32-sdk)).
- The code-first API has rough edges found while writing the POSIX demo: registering a settable property needs a live connection, the controller has no change callback, and float values are always formatted with `%f`.
- esp32-sdk is not public yet; links to it will resolve once it is.

## Quick start: the POSIX demo

You need CMake 3.18 or later, a C++17 compiler (GCC or Clang), and, for the end-to-end tests, `mosquitto` (Homebrew: `brew install mosquitto`; Debian/Ubuntu: `apt-get install mosquitto`).

```bash
cmake -S ports/posix -B ports/posix/build
cmake --build ports/posix/build -j
ctest --test-dir ports/posix/build --output-on-failure
```

Then run the demo device and controller against a broker:

```bash
ports/posix/build/ebus-posix-device --host <broker> --device-id posix-demo
ports/posix/build/ebus-posix-controller --host <broker>
ports/posix/build/ebus-posix-controller --host <broker> --set posix-demo/switch/on=true --duration-s 5
```

The device publishes `ebus/5/posix-demo` with a settable `switch/on` and a simulated `sensor/temperature`, plus a child device. The [POSIX port README](ports/posix/README.md) has every option and how the port meets the core's contract.

## Building the core and its tests

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

As the top-level project this builds the libraries below, compiles each public header on its own, and runs the Unity suites in [`test/`](test/). ArduinoJson 7.4.3 and Unity 2.6.1 are fetched and checked against their SHA-256. A parent CMake project uses `add_subdirectory(<path>/cpp-sdk)` and links `ebus_core`; the tests are then off.

| Target | Contents | Depends on | Docs |
|---|---|---|---|
| `ebus_mqtt` | The MQTT transport interface (`MqttTransport`), the publishes held while the link is down (`PublishHold`), the reconnect order (`mqtt_after_connect()`) | nothing | [`doc/mqtt.md`](doc/mqtt.md) |
| `ebus_core` | Everything: the Homie model, `/set` dispatch, the controller, validation, broker discovery, and `ebus_mqtt` | `ebus_mqtt`, ArduinoJson | [`doc/core.md`](doc/core.md) |

`ebus_mqtt` builds with only its own include directory (`mqtt/include`), so a project can take it alone with `add_subdirectory(<path>/cpp-sdk/mqtt)`.

## Writing a port

A port binds four things, all documented in [`doc/core.md`](doc/core.md) ("What a port implements"):

| Piece | Header | What the port does |
|---|---|---|
| MQTT transport | `homie/homie_transport.h` | Subclasses `HomieTransport`, which is `ebus_mqtt`'s `MqttTransport` plus a Homie adapter: `publish()`, `subscribe()` and `queue_publish()` with a completion callback ([`doc/mqtt.md`](doc/mqtt.md)) |
| Clock | `homie/homie_clock.h` | `homie_clock_bind(now_ms, sleep_ms)` |
| Console | `homie/homie_log.h` | `homie_log_bind(vprintf_sink)`; optional, stdout otherwise |
| Settable table | `homie/homie_settable.h` | Supplies storage to `settable_table_bind()`, routes each claimed `/set` message to `settable_dispatch()` on its own task, and subscribes every settable topic after each connect |

The POSIX port is the reference: [`ports/posix/src/posix_port.cpp`](ports/posix/src/posix_port.cpp) binds the clock, console and settable table, and [`ports/posix/src/paho_transport.cpp`](ports/posix/src/paho_transport.cpp) is the transport. A port should also follow the rules for publishes made while the broker link is down and the reconnect order, both from [ebus-mqtt-client](https://github.com/electrification-bus/ebus-mqtt-client); `ebus_mqtt` provides them as `PublishHold` and `mqtt_after_connect()`, which the POSIX port uses.

## Using the core from PlatformIO

[`library.json`](library.json) makes this repository one PlatformIO library named `ebus_core` that compiles `src/` and `mqtt/src/`, puts `include/` and `mqtt/include/` on the include path, and depends on ArduinoJson 7.4.3. Add it with a git URL:

```ini
lib_deps =
    https://github.com/electrification-bus/cpp-sdk.git#<tag>
build_flags =
    -std=gnu++17
    -DUSE_EBUS_TOPIC
build_unflags =
    -std=gnu++11
```

The core needs C++17; `build_unflags` drops the gnu++11 default of arduino-esp32. It is compiled without the project's `build_src_flags`, so a macro it reads (`USE_EBUS_TOPIC`, `MAX_DATA_LEN`, `CONTROLLER_INBOX_BYTES`) goes in `build_flags`. esp32-sdk carries the same code in `lib/ebus_core/` until it moves to this dependency.

## License

[MIT License](LICENSE) - Copyright (c) 2026 Clark Communications Corporation. Fetched dependencies and their licenses: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Authors

Split out of [esp32-sdk](https://github.com/electrification-bus/esp32-sdk), with its history. Developed by:

- **Lead developer:** Doug Mendonça ([@nesl-admin](https://github.com/nesl-admin)), New Energy Solutions Lab, [doug@newenergysolutionslab.com](mailto:doug@newenergysolutionslab.com). Primary author of the firmware this code was split from, and a significant contributor to the eBus protocol and ecosystem more broadly.
- **Project owner:** Donald Clark Jackson ([@dcj](https://github.com/dcj)), Clark Communications Corporation, [dcj@clark-communications.com](mailto:dcj@clark-communications.com). Project direction and documentation.
