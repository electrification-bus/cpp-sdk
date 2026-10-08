# Third-party licenses

Nothing is vendored. CMake fetches each library below at configure time, pinned by release and SHA-256; PlatformIO resolves ArduinoJson from `library.json`.

| Library | Version | Used by | Linked into | License |
|---------|---------|---------|-------------|---------|
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | 7.4.3 | The core (`CMakeLists.txt`, `library.json`) | Every program that uses the core (header-only) | MIT |
| [Eclipse Paho MQTT C](https://github.com/eclipse-paho/paho.mqtt.c) | 1.3.16 | The POSIX port (`ports/posix/CMakeLists.txt`), unless an installed Paho is found | The POSIX port's programs, statically | EPL-2.0 or EDL-1.0 (BSD-3-Clause), at your option |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | 2.6.1 | The core's unit tests (`test/CMakeLists.txt`) | The test executables only | MIT |

## Obligations when distributing binaries

Ship ArduinoJson's license text and copyright notice with any binary built from the core. A binary that links Paho MQTT C carries its notices under the license you choose; under EDL-1.0 (BSD-3-Clause), that is the copyright notice, the license conditions and the disclaimer.
