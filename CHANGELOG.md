# Changelog

All notable changes to `cpp-sdk` are recorded here. Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.5.0] - 2026-10-10

### Added

- `ebus_link`, a CMake target over `ebus_homie` (`link/`, [doc/link.md](doc/link.md)): `EbusLink` copies up to three property values, local or remote, into a settable property, with a format, rounding, change detection, retry backoff, and the last good value kept on a retraction. In a device (`EbusLink::DEVICE`) it reads the root `Device`'s properties, watches remote ones through the settable table and sets a local target with `deliver_local_set()`; in a controller (`EbusLink::CONTROLLER`) it reads the discovery cache, binds `*` in the device or node part once discovery has settled, refuses an ambiguous match, and commands the target only while it is `ready`. The logic comes from esp32-sdk's `logic/link`, by Doug Mendonça. `ebus_core` links it, PlatformIO builds `link/src/`, and a CI step compiles it with only its own and `ebus_homie`'s include directories.
- Unity suites `test_link_core` and `test_link_engine`, which link `ebus_link`.
- `--link SOURCES=>TARGET` (with `--link-format`, `--link-decimals`, `--link-interval-ms`) for `ebus-posix-device` and `ebus-posix-controller`, and the end-to-end tests `e2e_link` and `e2e_link_device`.

## [0.4.1] - 2026-10-10

### Added

- `Property::has_value()`, and for the controller's copy of a remote property `Property::store_received()` and `Property::forget_value()` ([doc/core.md](doc/core.md#controller-values)).
- `controller_device_count()` and `controller_device_at()`, which return a pointer into the controller's device table, so a caller need not copy every `ControllerDevice` with `controller_list_device_info()`.

### Fixed

- A zero-length payload on a property topic (the retained value removed) set the controller's copy to false, 0 or 0.000000 by datatype. It now clears `has_value()` and leaves `value()` at the last payload.
- The controller's `value()` of a received property is the payload as published. It was reformatted by datatype, so a float `21.37` read back as `21.370001`.

## [0.4.0] - 2026-10-09

### Added

- `ebus_homie`, a CMake target (`homie/`, [doc/core.md](doc/core.md)): the Homie model, `/set` dispatch, the controller, id and payload validation, the JSON helpers, `HomieTransport`, and the log and clock hooks. It depends on `ebus_mqtt` and ArduinoJson, not on `ebus_discovery`. A CI step compiles it with only its own, `ebus_mqtt`'s and ArduinoJson's include directories. ([#3](https://github.com/electrification-bus/cpp-sdk/issues/3))

### Changed

- The Homie headers move to `<ebus/homie/...>`, and `util/jsonUtils.h` to `<ebus/homie/jsonUtils.h>`; `<homie/...>` and `<util/jsonUtils.h>` forward to them. `include/homie/homie_limits.h` also repeats the four base limits esp32-sdk's generator reads from it.
- `ebus_core` is an INTERFACE target that links `ebus_mqtt`, `ebus_discovery` and `ebus_homie`. The Homie suites link `ebus_homie` alone. PlatformIO still builds one library.

## [0.3.0] - 2026-10-09

### Added

- `ebus_discovery`, a CMake target with no dependency (`discovery/`, [doc/discovery.md](doc/discovery.md)): `txt_build_ebus()`, `txt_build_device_info()`, `txt_build_http()` and `txt_build_log()`, which build each advertised service's TXT record from one `EbusIdentity` into a fixed-size `TxtRecord`, with the keys and order of framework.md, ebus-service-discovery and esp32-sdk; `MdnsBackend`, the advertise, browse and resolve interface a port implements over its mDNS stack; `broker_browse()` and `broker_reresolve()`, esp32-sdk's broker selection over that interface; `broker_service_type()`. `ebus_core` links it. PlatformIO still builds one library.
- Unity suites `test_discovery_txt` and `test_discovery_backend`, which link `ebus_discovery` alone, and a CI step that compiles `ebus_discovery` with only its own include directory.
- `Device::forgetDescriptionHash()`: the next `publish()` of that device sends `$description` even if it is unchanged. A port calls it on every device before a recovery republish ([doc/core.md](doc/core.md#after-a-reconnect)); the no-op skip in `notifyStructuralChange()` stays. ([#9](https://github.com/electrification-bus/cpp-sdk/issues/9))

### Changed

- `broker_discovery.h` and `mdns_strings.h` move to `ebus_discovery` as `<ebus/discovery/broker_discovery.h>` and `<ebus/discovery/mdns_strings.h>`; `<platform/broker_discovery.h>` and `<platform/mdns_strings.h>` forward to them. Their suite is now `test_discovery_broker`. `mdns_strings.h` adds the full service types (`MDNS_TYPE_*`), `broker_ca`, `hw_version`, `broker-host` and each auth method value.

### Fixed

- The POSIX demo device republishes the whole tree, `$description` included, on every reconnect (was `publishStateTree()`), so a broker restarted without persistence describes it again. The `reconnect` end-to-end test checks that the root and child `$description` are retained after the restart.
- `doc/mqtt.md` listed esp32-sdk's `MqttClientTransport` as overriding the Property `queue_publish()`; it overrides the generic one.

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

[Unreleased]: https://github.com/electrification-bus/cpp-sdk/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/electrification-bus/cpp-sdk/compare/v0.4.1...v0.5.0
[0.4.1]: https://github.com/electrification-bus/cpp-sdk/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/electrification-bus/cpp-sdk/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/electrification-bus/cpp-sdk/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/electrification-bus/cpp-sdk/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/electrification-bus/cpp-sdk/releases/tag/v0.1.0
