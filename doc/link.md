# ebus_link: property links

A link copies up to three property values into one settable property: a sensor reading onto a display, a state onto a relay, a reading from one device onto another device. Each link is one `EbusLink` (`ebus/link/link.h`), configured with strings, so a port can build links from a config file or command-line flags without code per link. The pure functions underneath (reference parsing, `*` matching, rounding, `%` substitution, retry backoff) are in `ebus/link/link_core.h`.

The logic comes from Doug Mendonça's `logic/link` component in [esp32-sdk](https://github.com/electrification-bus/esp32-sdk).

```cpp
#include <ebus/link/link.h>

// In a device: two readings of this device onto its own display.
static EbusLink climate("climate-to-display", "env/temperature, env/humidity",
                        "display/two-line", "%1 °C|%2 %%", 1);
climate.setup(EbusLink::DEVICE, &root);   // after the tree is built and the settable table bound
climate.loop();                           // every pass of the main loop

// In a controller: whichever discovered device has the reading, onto another device.
static EbusLink pressure("pressure-to-display", "*/environment-*/air-pressure",
                         "a4cf12e8d0b4/display/line-two", "%s", 0);
pressure.setup(EbusLink::CONTROLLER);     // after controller_init()
pressure.loop();                          // after controller_loop()
```

`climate` sends `21.4 °C|48.3 %` and updates it whenever either reading changes. A link has no Homie node of its own and nothing about it appears in `$description`.

## Configuration

| Argument | Default | Meaning |
|---|---|---|
| `id` | | Names the link in log lines (`LINK[<id>]: ...`) |
| `source` | | Up to **3** property references, comma-separated. Local and remote may be mixed. |
| `target` | | One **settable** property reference |
| `format` | `"%s"` | The text sent to the target (below) |
| `decimals` | `-1` | Round numeric values to this many places (at most 6); `-1` sends them as published |
| `interval_ms` | `1000` | How often local sources (and, in a controller, the discovery cache) are read, and the first retry delay after a failed delivery |

The strings are not copied: they must outlive the link. `setup()` returns false, and logs why, when the link is disabled; a disabled link's `loop()` does nothing. After `setup()` the link must not move, because the settable table holds pointers into it.

### Mode

The mode is an argument to `setup()`, so one library build serves devices and controllers, and a test can run both in one process.

| | `EbusLink::DEVICE` | `EbusLink::CONTROLLER` |
|---|---|---|
| `setup()` needs | the root `Device` | `controller_init()` before `loop()` |
| Ends | local or remote | remote only: a controller has no nodes |
| Local source | read from the root device every `interval_ms` | |
| Remote source | watched on the broker through the settable table (one subscription per topic) | read from the controller's discovery cache every `interval_ms`; no subscription of its own |
| `*` | refused | bound against the discovery cache |
| Local target | `deliver_local_set()`, no broker involved | |
| Remote target | published to its `/set` topic on the root's transport | `controller_set_property()`, only while the target is `ready` |
| Target comes back | waits for the next change | current text sent again at once |
| `loop()` runs | on the task that calls `settable_dispatch()` | after `controller_loop()`, on the same task |

### References

| Form | Means |
|---|---|
| `<node>/<property>` | a property on the root device (device mode) |
| `<device-id>/<node>/<property>` | a property on another device |
| `*/<node>/<property>` | whichever discovered device has it (controller mode; see [Binding `*`](#binding--controller-mode)) |

Use the ids exactly as they appear in `$description`. A part without `*` is passed through `sanitize_homie_id()`, so `Environment_SHT20` becomes `environment-sht20`. Each part must fit `homie_limits.h` (a device id 63 characters, a node or property id 31), and so must the topic built from them (`HOMIE_TOPIC_MAX`, 127); a link whose reference does not is disabled at setup.

### Format

| Token | Becomes |
|---|---|
| `%1`, `%2`, `%3` | the 1st, 2nd, 3rd source's value |
| `%s` | the same as `%1` |
| `%%` | a literal `%` |

Anything else is copied as written, including a `%2` when there is only one source. The substitution is done by hand, never by `printf`, so a stray `%d` is just text. Text is UTF-8, and anything cut to fit (a value at 63 bytes, the text at 127) is cut on a character boundary.

### Rounding

`decimals` applies to every value that is a plain decimal number under 10^15 in magnitude: an optional sign, digits with an optional fraction, and an optional exponent of one or two digits. `21.37` becomes `21.4` at `1`, `49.6` becomes `50` at `0`, and `-0.04` becomes `0.0` (a zero reading carries no sign). Anything else passes through unchanged: `on`, `21.4 C`, `0x1A`, `nan`, or a number with spaces around it.

## Behavior

- **Nothing is sent until every source has a value**, so a target never shows a blank where a reading belongs.
- **After that, a source that goes empty keeps its last good value.** An empty payload on a watched topic, a controller copy whose retained value was removed (`has_value()` false), or a local property cleared with `forget_value()` is never forwarded.
- **The target is written only when the rendered text changes.**
- **A failed delivery is retried** after `interval_ms`, then after twice as long each time up to 30 s (`EBUS_LINK_RETRY_MAX_MS`), and logged when the failures start and when delivery recovers. A new value does not shorten the wait.
- **Remote values in device mode are acted on at once**: the next `loop()` after the settable table hands one over renders and delivers it. Local sources are read every `interval_ms`. On connect the broker hands over the retained value, so a remote source is filled as soon as MQTT is up.
- **A local target works with the broker down**, because `deliver_local_set()` calls the target's `/set` handler directly.
- **A value a local target refuses is not retried.** `deliver_local_set()` reports that a settable property is registered for the topic, not that the value was accepted; a value that fails validation or that its driver refuses counts as delivered and is sent again only when the text changes. The core logs the refusal.
- **In a controller, the target is commanded only while its effective state is `ready`**, and the current text is sent again each time it returns to `ready`, so a display that rebooted catches up at once. In a device, a `/set` sent while the target is offline is lost until the source next changes.

## Binding `*` (controller mode)

`*` may appear in the device part, wholly or within it (`a4cf*`), and in the node part (`environment-*`). A part without `*` must match exactly. `*` in the property is refused at setup: the property is what `%1`, `%2` and `%3` refer to.

- **What it matches**: the discovered device and node that match the patterns **and** have the property; for a target, the property must also be settable.
- **It binds only once discovery has settled**, which is when every device the controller knows has a `$description`. Until then the link sends nothing, and says so when the wait starts and again at most once a minute (`EBUS_LINK_REMINDER_MS`). Nothing times this out. A live device republishes its `$description` when it reconnects, and one the controller's inbox dropped is re-subscribed a few seconds later; a wait that does not end means a retained `$state` with no `$description` behind it, a `$description` the controller cannot parse, or one over `MAX_DATA_LEN` (8192 bytes).
- **Settled means every known device has described itself**, not that every device is known. Past `MAX_DISCOVERED_DEVICES` (16) further devices are not tracked, so a pattern can bind although an untracked device also matches.
- **Ambiguity is refused.** A pattern that matches more than one node, on several devices or on one, stays unbound for the life of the link, which then sends nothing, and the log names the candidates. A pattern that matches nothing is retried every `interval_ms`.
- **Binding is sticky.** Once bound, a reference keeps that device for the life of the link, so a device that goes `lost` and comes back resumes the same link.

## Limits

| Limit | Value |
|---|---|
| Sources per link | 3 (`EbusLink::MAX_SOURCES`) |
| A source value | 63 bytes (`EBUS_LINK_VALUE_MAX`, NUL included) |
| Rendered text | 127 bytes (`EbusLink::TEXT_MAX`, NUL included) |
| Remote sources in device mode | one link source per topic: the settable table keeps one handler per topic, so a second link (or the same link twice) naming the same remote property is disabled at setup; a full table disables the link too |

Give each link its own target: two links writing one target overwrite each other on every change. To show several values in one place, use one link with several sources.

## Log lines

| Line | Cause |
|---|---|
| `bad source list '...'` / `bad target '...'` | Not `a/b` or `a/b/c`, an empty part, a part over its limit, a malformed pattern, `*` in the property, more than 3 sources, or a two-part reference in a controller link. The link is disabled. |
| `... uses *, which needs the controller's discovery` | `*` in a device link. The link is disabled. |
| `target topic for ... is over 127 chars` / `source %N topic is over 127 chars` | The topic does not fit `HOMIE_TOPIC_MAX`. The link is disabled. |
| `source %N ... is already watched by another link source` | Device mode: two link sources name the same remote property. The link is disabled. |
| `source %N ... could not be registered` | Device mode: the settable table is full. The link is disabled. |
| `source x/y not found on this device` | No local node or property by that id, said once. |
| `delivery to ... failed; retrying, backing off from N ms to 30000 ms` | A remote target with the broker down, or a local target not registered. `delivered to ...` follows when it recovers. |
| `waiting for discovery to settle: ...` | Controller mode: a pattern waits for every known device's `$description`. |
| `... matched no discovered ...; retrying` / `... matches more than one node (...); not bound` | Controller mode: a pattern found no match yet, or more than one. |
| `target ... is ready; sending current text` | Controller mode: the target came (back) to `ready`. |

## In the POSIX port

`ebus-posix-device` and `ebus-posix-controller` take `--link SOURCES=>TARGET` (repeatable), each followed by optional `--link-format`, `--link-decimals` and `--link-interval-ms`; the links are named `link-1`, `link-2`, and so on. The end-to-end tests `e2e_link` (a controller link with a device-segment pattern) and `e2e_link_device` (a device link from another device's property to its own) run them against mosquitto.

```bash
ebus-posix-device --device-id lamp --link 'posix-demo/switch/on=>switch/on'
ebus-posix-controller --link 'posix-demo/switch/on=>lamp*/switch/on' --link-interval-ms 200
```
