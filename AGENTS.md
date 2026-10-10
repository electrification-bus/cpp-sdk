# cpp-sdk

Instructions for LLM coding assistants working in this repository. Personal additions go in a gitignored local file (`CLAUDE.local.md` for Claude Code); see [CONTRIBUTING.md](CONTRIBUTING.md#llm-coding-assistants).

## What this is

A portable C++17 core for devices and controllers on the Electrification Bus (eBus), which builds on the [Homie 5 MQTT convention](https://homieiot.github.io/). It depends only on the C and C++ standard libraries and ArduinoJson; a port supplies the MQTT client, the console and the clock. [esp32-firmware](https://github.com/electrification-bus/esp32-firmware) consumes it as a git submodule, pinned to a release tag.

## Layout

| Path | Contents |
|---|---|
| `mqtt/` | `ebus_mqtt`: the transport interface, publishes held while disconnected, the reconnect order ([doc/mqtt.md](doc/mqtt.md)) |
| `discovery/` | `ebus_discovery`: mDNS TXT records, the `MdnsBackend` interface, broker discovery ([doc/discovery.md](doc/discovery.md)) |
| `homie/` | `ebus_homie`: Device, Node, Property, `/set` dispatch, the controller, validation ([doc/core.md](doc/core.md)) |
| `link/` | `ebus_link`: property links ([doc/link.md](doc/link.md)) |
| `include/` | The pre-split header paths (`<homie/Device.h>`), forwarding to the components |
| `ports/posix/` | The POSIX reference port (Paho MQTT C), its demo programs and end-to-end tests |
| `test/` | Unity suites, one directory per suite; `test/support/` holds the fakes that stand in for a port |

## Commands

```sh
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
cmake -S ports/posix -B ports/posix/build && cmake --build ports/posix/build -j && ctest --test-dir ports/posix/build --output-on-failure
```

The first runs the core's suites; the second builds the POSIX port and runs its end-to-end tests, which need `mosquitto` installed. A change to what goes on the wire needs the end-to-end tests too, not only a unit test.

## House style for the core

`mqtt/`, `discovery/`, `homie/`, `link/` and `include/` run on microcontrollers:

- No dynamic allocation in steady state: fixed-size buffers and static or caller-owned storage. What allocates today is listed in [doc/core.md](doc/core.md) ("Rules for code in the core"); new code adds nothing to that list.
- No STL, no Arduino `String`, no lambdas. Use `snprintf`, and C function pointers with a `void*` context for callbacks. Members are underscore-prefixed.
- No Arduino, ESP-IDF, FreeRTOS or OS header. Each component includes only standard headers, its own, and those of the components it depends on; CI compiles each with only those include directories.
- C++17, GCC and Clang, warnings as errors in CI.
- Ports (`ports/`) may use their platform's facilities but keep the contract in [doc/core.md](doc/core.md).
- The Homie 5 convention and the [eBus specification](https://github.com/electrification-bus/specification) decide wire behavior: where code and spec disagree, fix the code. On a spec sync, update `.ebus-spec.json`.
- Comments explain a non-obvious why, not what the code does.
- User-visible changes get an entry under `## [Unreleased]` in [CHANGELOG.md](CHANGELOG.md).

Markdown prose is written one paragraph per line, without hard wraps.
