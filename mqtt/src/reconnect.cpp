// SPDX-License-Identifier: MIT
#include <ebus/mqtt/reconnect.h>

bool mqtt_after_connect(PublishHold* hold, const MqttConnectSteps& steps, bool first,
                        MqttConnectReport* report) {
    int held = hold ? hold->count() : 0;
    int flushed = hold ? hold->flush(steps.send, steps.ctx) : 0;
    if (report) {
        report->held = held;
        report->flushed = flushed;
    }
    if (flushed < held) return false;
    if (steps.resubscribe && !steps.resubscribe(steps.ctx)) return false;
    if (steps.notify) steps.notify(steps.ctx, first);
    return true;
}
