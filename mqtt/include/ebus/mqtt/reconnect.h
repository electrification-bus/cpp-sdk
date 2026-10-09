#pragma once
// The order of work after every successful connect, ebus-mqtt-client's:
//
//   1. Flush the hold. What was published while the link was down describes state that
//      already exists, so it reaches the broker before anything reacts to the connect.
//   2. Subscribe every topic again (a clean session keeps none).
//   3. Notify the application, which may now publish (a Homie device republishes its
//      state) and subscribe.
//
// Until step 1 has finished, the port keeps treating the link as down, so a publish made
// meanwhile joins the hold behind the older values instead of overtaking them.
// mqtt_after_connect() runs the three steps on the task that owns the client.
#include <ebus/mqtt/publish_hold.h>

struct MqttConnectSteps {
    // Step 1: send one held publish on the live link, as PublishHold::flush() calls it.
    PublishHold::send_fn send;
    // Step 2: mark the link usable for publish() and subscribe(), then subscribe every
    // topic. Returns false when the link dropped meanwhile. May be null.
    bool (*resubscribe)(void* ctx);
    // Step 3: `first` is true for the first connect. May be null.
    void (*notify)(void* ctx, bool first);
    void* ctx;
};

struct MqttConnectReport {
    int held;      // entries in the hold when the connect came up
    int flushed;   // entries sent by step 1
};

// Runs the three steps in order. Returns true when all three ran. Returns false, without
// running the later steps, when the flush stopped early (the rest stays held) or
// resubscribe() reported the link down; the port then treats the link as down and runs
// this again after its next connect. `hold` may be null for a port that holds nothing.
bool mqtt_after_connect(PublishHold* hold, const MqttConnectSteps& steps, bool first,
                        MqttConnectReport* report = nullptr);
