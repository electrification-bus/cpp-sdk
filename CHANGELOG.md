# Changelog

All notable changes to `cpp-sdk` are recorded here. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-10-08

### Added

- The portable core, split out of [esp32-sdk](https://github.com/electrification-bus/esp32-sdk)'s `lib/ebus_core/` with its history: the Homie 5 Device, Node and Property model, `/set` dispatch and the settable table, the controller, id and payload validation, broker-discovery parsing, and the `HomieTransport`, log and clock interfaces a port implements.
- The POSIX port (`ports/posix/`): a `HomieTransport` over Eclipse Paho MQTT C, a demo device and controller, and end-to-end tests against mosquitto.
- The core's Unity suites, built and run with CMake and CTest.
- `library.json`, so a PlatformIO project can add the core with a `lib_deps` git URL.

[Unreleased]: https://github.com/electrification-bus/cpp-sdk/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/electrification-bus/cpp-sdk/releases/tag/v0.1.0
