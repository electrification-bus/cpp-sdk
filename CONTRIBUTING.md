# Contributing to cpp-sdk

Thanks for your interest in contributing! `cpp-sdk` is a portable C++ core for eBus devices and controllers using the [Homie 5 MQTT convention](https://homieiot.github.io/), with a POSIX reference port. Platform ports supply the MQTT client, console and clock; [esp32-sdk](https://github.com/electrification-bus/esp32-sdk) is the ESP32 port.

## How to contribute

### Discussions

Use [Discussions](https://github.com/electrification-bus/cpp-sdk/discussions) for:

- Open-ended questions about the core's design, scope, or intent
- Proposed new ports (Zephyr, FreeRTOS on STM32 or NXP, embedded Linux) or changes to the port interfaces: worth aligning on the contract before writing the code
- Integration questions ("how do I model my device as a nested Homie 5 device?") that aren't yet a clear bug or feature request
- Thinking out loud about a proposed change before scoping it

Aligned outcomes often turn into one or more Issues or pull requests.

### Issues

Use [Issues](https://github.com/electrification-bus/cpp-sdk/issues) for actionable changes:

- Bug reports with reproduction steps (port, platform, compiler, broker, and ideally a failing test or log)
- Concrete feature requests with a clear scope and a use case
- Documentation gaps where a specific doc or comment change is intended
- Discussion outcomes that have alignment and a clear scope

If you're not sure whether something is an Issue or a Discussion, start with a Discussion; we can convert it later.

### Pull requests

Pull requests are welcome.

- For small fixes (typos, doc tweaks, low-risk bug fixes), open a PR directly.
- For substantive changes (new public API, changes to `HomieTransport` or the other port interfaces, new ports, changes that alter Homie/eBus wire behavior), open a Discussion or Issue first so we can align on scope before you invest the effort.
- **Spec conformance is the north star.** The [Homie 5 convention](https://homieiot.github.io/) and the [Electrification Bus specification](https://github.com/electrification-bus/specification) are the source of truth for on-the-wire behavior: topic structure, datatypes, `$state` semantics, the nested/proxied device model, the eBus device- and capability-type vocabularies. When the code and the spec disagree, the spec wins; fix the code rather than codifying the deviation.
- **Update `.ebus-spec.json` on every spec sync.** When you reconcile the core with a newer specification commit, bump `synced_commit`, `synced_date`, and `framework`, and edit `supports` to match the framework features the code now implements (see the specification's `conventions/spec-provenance.md`).
- **House style for the core (`include/`, `src/`, `mqtt/`).** It runs on microcontrollers, so:
  - No dynamic allocation in steady state. Use fixed-size buffers and static or caller-owned storage; what allocates today is listed in [doc/core.md](doc/core.md) ("Rules for code in the core"), and new code adds nothing to that list.
  - No STL, no Arduino `String`, no lambdas. Use `snprintf` over string concatenation, and the C function-pointer + `void*` context idiom for callbacks. Members are underscore-prefixed.
  - No Arduino, ESP-IDF, FreeRTOS or OS header. The core includes only standard headers, ArduinoJson and other core headers, and `ebus_mqtt` (`mqtt/`) only standard headers and its own; anything platform-specific goes through the port interfaces. CI builds each with only its own include directory and fails on anything else.
  - C++17, GCC or Clang, warnings as errors in CI.
- **Ports (`ports/`) may use their platform's facilities**, but keep the core's contract as documented in [doc/core.md](doc/core.md), including the rules for publishes while the broker link is down.
- **Run the tests.** `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure` runs the core's Unity suites; the POSIX port's tests (`ctest --test-dir ports/posix/build`) need `mosquitto`. See the [README](README.md). To add a core suite, create `test/test_<name>/test_<name>.cpp` with its own `main()`; `test/CMakeLists.txt` picks it up. A suite named `test_mqtt_<name>` links `ebus_mqtt` alone. The fakes in `test/support/` stand in for a port. CI runs both test sets with GCC and Clang on every push.
- **Test wire changes against a real broker.** A unit test is necessary but not sufficient for anything that changes what goes on the wire; the POSIX port's end-to-end tests and its demo programs run the core against mosquitto.
- **Respect upstream licenses: reference, don't copy.** This SDK is MIT. Do **not** paste source from copyleft projects into it. PRs that copy GPL/copyleft code will be asked to reimplement.
- **Keep comments to a minimum.** Write self-explanatory code and reserve comments for non-obvious *why* (a spec corner, a platform quirk). Don't add comments that just restate the code.
- **Don't hard-wrap Markdown prose.** Write each paragraph in docs as a single continuous line; reserve hard breaks for list items, headings, code fences, and tables.
- One commit per logical change is fine; we don't require squash or any particular branch naming.

## Keep your personal LLM config out of the repo

If you use an LLM coding assistant (Claude Code, Cursor, Aider, etc.), do **not** commit your personal LLM config files (`CLAUDE.md`, `AGENTS.md`, `.claude/`, `.cursor/`, etc.). Different contributors have different setups. Keep them in a separate shadow repo symlinked into your working tree, or add the paths to `.git/info/exclude`. The project `.gitignore` already lists the common ones defensively. If you have a use case for project-wide LLM context that *should* be tracked, discuss with the maintainers first.

## Code of conduct

Be respectful and constructive. We appreciate everyone who takes the time to file an issue, start a discussion, or send a pull request.

## Maintenance posture

`cpp-sdk` is an active alpha project. Updates and maintenance, including responses to issues filed on GitHub, will take place on an "as time and resources permit" basis. It is maintained alongside the rest of the eBus ecosystem, the [Electrification Bus specification](https://github.com/electrification-bus/specification), [`esp32-sdk`](https://github.com/electrification-bus/esp32-sdk), [`python-sdk`](https://github.com/electrification-bus/python-sdk), and [`ebus-mqtt-client`](https://github.com/electrification-bus/ebus-mqtt-client); see the specification repo's README §Governance for the project's long-term governance context.
