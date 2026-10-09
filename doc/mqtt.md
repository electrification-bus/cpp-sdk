# ebus_mqtt

The MQTT layer under the Homie model, with no Homie in it: the transport interface a port implements, the publishes held while the broker link is down, and the order of work after a connect. It depends on the C and C++ standard libraries only, the same split as the Python [ebus-mqtt-client](https://github.com/electrification-bus/ebus-mqtt-client). Sources are in `mqtt/`; CMake builds them as the `ebus_mqtt` target, which `ebus_core` links.

| Header | Contents |
|---|---|
| `ebus/mqtt/transport.h` | `MqttTransport`, the interface a port implements; `mqtt_publish_done_fn` |
| `ebus/mqtt/publish_hold.h` | `PublishHold`, the disconnected-link rules, in fixed storage |
| `ebus/mqtt/reconnect.h` | `mqtt_after_connect()`, the reconnect order |

## The transport

```cpp
typedef void (*mqtt_publish_done_fn)(void* ctx, const char* payload, int length, bool sent);

class MqttTransport {
 public:
    virtual bool publish(const char* topic, const char* payload, int length, bool retained, int qos) = 0;
    virtual bool queue_publish(const char* topic, const char* payload, int length, bool retained, int qos,
                               mqtt_publish_done_fn done, void* ctx) = 0;
    virtual bool subscribe(const char* topic, int qos) = 0;
    virtual bool connected() = 0;
    virtual int last_error() = 0;
};
```

`publish()` and `subscribe()` run on the task that owns the client, never inside its receive callback. `queue_publish()` may be called from any task; the port copies the message for the owning task and calls `done(ctx, payload, length, sent)` exactly once per call when `done` is set, including when the call itself fails.

The Homie layer uses `HomieTransport` (`ebus/homie/homie_transport.h`), which derives from `MqttTransport` and adds `queue_publish(topic, payload, length, retained, Property* source)`. Property calls that overload; by default it forwards to the generic one at `homie_qos(retained)`, with a callback that calls `source->queued_publish_done()`. A port overrides one of the two:

| Overrides | Ports |
|---|---|
| `queue_publish(..., qos, done, ctx)` | `PahoTransport` in `ports/posix/`; esp32-sdk's `MqttClientTransport` |
| `queue_publish(..., Property* source)` | A port written against 0.1.0 |

A `HomieTransport` that overrides neither refuses every queued publish, logs an error, and reports it to `done` as not sent.

## While the link is down

`PublishHold` applies ebus-mqtt-client's rules ("Publishing before the connection is up"):

- Retained, any QoS: held, newest value per topic; a newer value moves to the back of the flush order.
- Not retained, QoS 1 or 2: held in order, one entry per publish.
- Not retained, QoS 0: dropped (`DROPPED_QOS0`). Every non-retained Homie publication is QoS 0, so on a Homie device only retained values are held.
- At most `EBUS_MQTT_HOLD_ENTRIES` (64) entries, evicting the oldest (`EVICTED_OLDEST`). A topic over `EBUS_MQTT_HOLD_TOPIC_MAX` (127 characters) or a payload over `EBUS_MQTT_HOLD_PAYLOAD_MAX` (1024 bytes, so a `$description`) is dropped (`DROPPED_TOO_LARGE`).

`hold()` returns what it did and does not log, so the port decides what to log. `flush(send, ctx)` sends the oldest first and stops at the first send that returns false, keeping that entry and the rest. The storage is in the object (about 75 KB with the defaults), so declare it static. The three sizes set the class layout: an override goes in the flags of every translation unit that includes the header (PlatformIO `build_flags`; CMake `target_compile_definitions(ebus_mqtt PUBLIC ...)`). Only the task that owns the client may touch it.

A port marks the link down when it is lost and, while it is down, passes each `publish()` to `hold()` instead of the client.

## After a connect

Every successful connect runs, in order:

1. Flush the hold. What was published while the link was down describes state that already exists, so it reaches the broker before anything reacts to the connect.
2. Subscribe every topic again.
3. Notify the application, which may now publish and subscribe.

Until the flush has finished the port keeps treating the link as down, so a publish made meanwhile joins the hold behind older values instead of overtaking them. `mqtt_after_connect(hold, steps, first, report)` runs the three steps from hooks in `MqttConnectSteps` (`send`, `resubscribe`, `notify`, `ctx`). The `resubscribe` hook marks the link usable for `publish()` and `subscribe()` before subscribing. The function returns false, without the later steps, when the flush stops early or `resubscribe` reports the link lost; the port then treats the link as down and runs it again after the next connect. `report` gives the number held and the number flushed. `PahoTransport::try_connect()` in [`ports/posix/src/paho_transport.cpp`](../ports/posix/src/paho_transport.cpp) is the reference.

## Building

`ebus_mqtt` has `mqtt/include` as its only include directory. CI (`core-host-build`, step "ebus_mqtt alone") compiles each source and header in `mqtt/` with that one `-I` and nothing else, and the `ebus_mqtt_header_check` target compiles each header against `ebus_mqtt` alone. Its Unity suites are `test/test_mqtt_*`, which link `ebus_mqtt` without `ebus_core`. A project that wants only this layer can `add_subdirectory(<path>/cpp-sdk/mqtt)`.

PlatformIO builds it as part of the single `ebus_core` library: `library.json` compiles `mqtt/src/` and adds `mqtt/include` to the include path, which PlatformIO also gives the library's dependents.
