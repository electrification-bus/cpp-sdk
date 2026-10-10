// EbusLink's CONTROLLER mode: binding * against the discovery cache, reading sources from
// it, and commanding the target with controller_set_property(). Only this file references
// the controller, and link.cpp reaches it only through the table that
// ebus_link_enable_controller() installs, so a device build links neither.
#include <ebus/link/link.h>

#include <ebus/homie/Device.h>
#include <ebus/homie/Node.h>
#include <ebus/homie/Property.h>
#include <ebus/homie/controller.h>
#include <ebus/homie/homie.h>
#include <ebus/homie/homie_clock.h>
#include <ebus/homie/homie_log.h>
#include <stdio.h>
#include <string.h>

#include "link_internal.h"

void ebus_link_enable_controller() {
    static const EbusLink::ControllerOps ops = {&EbusLink::controller_poll,
                                                &EbusLink::controller_deliver};
    EbusLink::_controller_ops = &ops;
}

// ── binding * references (CONTROLLER mode) ─────────────────────────────────────────────
// Devices announce themselves with $state; their nodes and properties arrive only with
// $description. A pattern is matched only once EVERY discovered device has described
// itself: earlier, "the one device with this property" means "the one whose description
// the broker happened to deliver first", which differs between boots. Nothing times this
// out; doc/link.md says what keeps discovery from settling.
static bool discovery_settled(char* waiting_on, size_t waiting_len, int* missing, int* total) {
    int n = controller_device_count();
    *total = n;
    *missing = 0;
    if (waiting_len) waiting_on[0] = '\0';
    for (int i = 0; i < n; i++) {
        const ControllerDevice* cd = controller_device_at(i);
        if (!cd || cd->has_description) continue;
        (*missing)++;
        const char* id = cd->device ? cd->device->getId() : "?";
        size_t used = strlen(waiting_on);
        snprintf(waiting_on + used, waiting_len - used, "%s%s", used ? ", " : "", id);
    }
    return n > 0 && *missing == 0;
}

// Every discovered device/node pair matching the patterns that has `prop` (settable, with
// `want_settable`, so a target cannot bind to a reading) is added to `out`.
static void resolve_ref(const char* dev_pat, const char* node_pat, const char* prop,
                        bool want_settable, EbusLinkResolve* out) {
    int n = controller_device_count();
    for (int i = 0; i < n; i++) {
        const ControllerDevice* cd = controller_device_at(i);
        Device* d = cd ? cd->device : nullptr;
        if (!d) continue;
        for (int j = 0; j < d->numNodes(); j++) {
            Node* nd = d->nodeAt(j);
            if (!nd || !ebus_link_ref_matches(dev_pat, node_pat, d->getId(), nd->id())) continue;
            Property* pr = link_find_in_node(nd, prop);
            if (!pr || (want_settable && !pr->settable())) continue;
            // Two matching nodes on one device are as ambiguous as two devices.
            ebus_link_resolve_add(out, d->getId(), nd->id());
        }
    }
}

void EbusLink::resolve_source(int i, bool remind, bool* said) {
    EbusLinkSource& sc = _sources[i];
    if (!sc.wild || sc.refused) return;
    EbusLinkResolve r;
    resolve_ref(sc.dev, sc.node, sc.prop, false, &r);
    EbusLinkResolveResult res = ebus_link_resolve_result(&r);
    if (res == EBUS_LINK_RESOLVE_MANY) {
        sc.refused = true;
        homie_logf("LINK[%s]: source %%%d '%s/%s/%s' matches more than one node (%s); not "
                   "bound; name the device, or narrow the node\n",
                   _id, i + 1, sc.dev, sc.node, sc.prop, r.cands);
    } else if (res == EBUS_LINK_RESOLVE_ONE) {
        snprintf(sc.dev, sizeof(sc.dev), "%s", r.dev);
        snprintf(sc.node, sizeof(sc.node), "%s", r.node);
        sc.wild = false;
        homie_logf("LINK[%s]: source %%%d bound to %s/%s/%s\n", _id, i + 1, sc.dev, sc.node,
                   sc.prop);
    } else if (!sc.unmatched || remind) {
        homie_logf("LINK[%s]: source %%%d '%s/%s/%s' matched no discovered device; "
                   "retrying\n", _id, i + 1, sc.dev, sc.node, sc.prop);
        *said = true;
    }
    sc.unmatched = (res == EBUS_LINK_RESOLVE_NONE);
}

bool EbusLink::resolve_target(bool remind, bool* said) {
    if (!_tgt_wild || _tgt_refused) return true;
    EbusLinkResolve r;
    resolve_ref(_tgt_dev, _tgt_node, _tgt_prop, true, &r);
    EbusLinkResolveResult res = ebus_link_resolve_result(&r);
    if (res == EBUS_LINK_RESOLVE_MANY) {
        _tgt_refused = true;
        homie_logf("LINK[%s]: target '%s/%s/%s' matches more than one node (%s); not bound; "
                   "name the device, or narrow the node\n",
                   _id, _tgt_dev, _tgt_node, _tgt_prop, r.cands);
    } else if (res == EBUS_LINK_RESOLVE_ONE) {
        snprintf(_tgt_dev, sizeof(_tgt_dev), "%s", r.dev);
        snprintf(_tgt_node, sizeof(_tgt_node), "%s", r.node);
        // The /set topic is "<domain>/5/" plus this path plus "/set".
        int n = snprintf(_target_topic, sizeof(_target_topic), "%s/%s/%s", _tgt_dev,
                         _tgt_node, _tgt_prop);
        if (n < 0 || HOMIE_TOPIC_PREFIX_MAX + 1 + n + 4 > HOMIE_TOPIC_MAX) {
            _tgt_refused = true;
            _target_topic[0] = '\0';
            homie_logf("LINK[%s]: target bound to %s/%s, but its /set topic could be over %d "
                       "chars; not bound\n", _id, _tgt_dev, _tgt_node, HOMIE_TOPIC_MAX);
            return false;
        }
        _tgt_wild = false;
        homie_logf("LINK[%s]: target bound to %s -> %s\n", _id, _tgt_dev, _target_topic);
    } else if (!_tgt_unmatched || remind) {
        homie_logf("LINK[%s]: target '%s/%s/%s' matched no discovered settable property; "
                   "retrying\n", _id, _tgt_dev, _tgt_node, _tgt_prop);
        *said = true;
    }
    _tgt_unmatched = (res == EBUS_LINK_RESOLVE_NONE);
    return true;
}

// Bind every * reference to a real device. True when nothing is left unbound.
bool EbusLink::resolve_wildcards() {
    bool pending = _tgt_wild;
    for (int i = 0; i < _num_sources; i++) pending = pending || _sources[i].wild;
    if (!pending) return true;

    uint32_t now = homie_now_ms();
    char waiting[160];
    int missing = 0, total = 0;
    if (!discovery_settled(waiting, sizeof(waiting), &missing, &total)) {
        if (!_settle_logged || now - _settle_logged_ms >= EBUS_LINK_REMINDER_MS) {
            _settle_logged = true;
            _settle_logged_ms = now;
            if (total == 0) {
                homie_logf("LINK[%s]: waiting for discovery to settle: no devices yet\n", _id);
            } else {
                homie_logf("LINK[%s]: waiting for discovery to settle: %d of %d device(s) "
                           "without $description (%s)\n", _id, missing, total, waiting);
            }
        }
        return false;
    }

    // An unmatched pattern is retried every interval but said only when it starts, and
    // then as a reminder.
    bool remind = (now - _unmatched_said_ms >= EBUS_LINK_REMINDER_MS);
    bool said = false;
    for (int i = 0; i < _num_sources; i++) resolve_source(i, remind, &said);
    bool target_ok = resolve_target(remind, &said);
    if (said) _unmatched_said_ms = now;
    if (!target_ok || _tgt_wild || _tgt_refused) return false;
    for (int i = 0; i < _num_sources; i++) {
        if (_sources[i].wild || _sources[i].refused) return false;
    }
    return true;
}

// ── running ────────────────────────────────────────────────────────────────────────────

// Everything comes from the discovery cache, so it is polled on the interval. The target's
// effective state gates delivery: /set is not retained, so a command to a device that is
// not there is lost. Each time the target comes back to ready the current text is sent
// again, so a display that rebooted catches up at once. True when loop() should render and
// deliver.
bool EbusLink::controller_poll(uint32_t now) {
    if (now - _last_poll_ms < _interval_ms) return false;
    _last_poll_ms = now;
    // A pattern that has not bound, or refused to, leaves the link idle.
    if (!resolve_wildcards()) return false;

    bool ready = (controller_effective_state(_tgt_dev) == DEVICE_STATE_READY);
    if (ready && !_target_ready) {
        homie_logf("LINK[%s]: target %s is ready; sending current text\n", _id, _tgt_dev);
        _have_sent = false;
        _failing = false;   // a target that came back is tried at once
    }
    _target_ready = ready;
    for (int i = 0; i < _num_sources; i++) {
        EbusLinkSource& s = _sources[i];
        Property* prop = link_find_property(controller_get_device(s.dev), s.node, s.prop);
        if (prop) link_take_value(s, prop);
    }
    return ready;
}

// Counted in the controller's stats; non-retained, QoS 0 (Homie).
bool EbusLink::controller_deliver(const char* text) {
    return controller_set_property(_tgt_dev, _tgt_node, _tgt_prop, text);
}
