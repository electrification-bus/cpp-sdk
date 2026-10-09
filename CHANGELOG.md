# Changelog

All notable changes to `cpp-sdk` are recorded here. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-10-09

### Added

- `ebus_mqtt`, a CMake target with no dependency (`mqtt/`, [doc/mqtt.md](doc/mqtt.md)): `MqttTransport`, the transport interface, whose `queue_publish()` takes a QoS and a completion callback (`mqtt_publish_done_fn` and a `void*` context); `PublishHold`, the disconnected-link rules, moved from the POSIX port; and `mqtt_after_connect()`, which runs the reconnect order (flush, re-subscribe, notify). `ebus_core` links it. PlatformIO still builds one library.
- Unity suites `test_mqtt_publish_hold` and `test_mqtt_reconnect`, which link `ebus_mqtt` alone, and a CI step that compiles `ebus_mqtt` with only its own include directory.

### Changed

- `HomieTransport` derives from `MqttTransport`. Its `queue_publish(..., Property* source)` is no longer pure: it forwards to the generic overload with a callback that calls `Property::queued_publish_done()`. A port that overrides it, as esp32-sdk's does, builds unchanged; a new port overrides the generic overload instead.
- `PublishHold` sizes are `EBUS_MQTT_HOLD_ENTRIES`, `EBUS_MQTT_HOLD_TOPIC_MAX` and `EBUS_MQTT_HOLD_PAYLOAD_MAX` (were `EBUS_POSIX_HOLD_ENTRIES` and `EBUS_POSIX_HOLD_PAYLOAD_MAX`). It no longer logs; `hold()` returns `EVICTED_OLDEST` or `DROPPED_TOO_LARGE` and the port logs. Its header is `<ebus/mqtt/publish_hold.h>`.
- The POSIX port's `PahoTransport` overrides the generic `queue_publish()` and runs its connect through `mqtt_after_connect()`.

## [0.1.0] - 2026-10-08

### Added

- The portable core, split out of [esp32-sdk](https://github.com/electrification-bus/esp32-sdk)'s `lib/ebus_core/` with its history: the Homie 5 Device, Node and Property model, `/set` dispatch and the settable table, the controller, id and payload validation, broker-discovery parsing, and the `HomieTransport`, log and clock interfaces a port implements.
- The POSIX port (`ports/posix/`): a `HomieTransport` over Eclipse Paho MQTT C, a demo device and controller, and end-to-end tests against mosquitto.
- The core's Unity suites, built and run with CMake and CTest.
- `library.json`, so a PlatformIO project can add the core with a `lib_deps` git URL.

[Unreleased]: https://github.com/electrification-bus/cpp-sdk/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/electrification-bus/cpp-sdk/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/electrification-bus/cpp-sdk/releases/tag/v0.1.0
