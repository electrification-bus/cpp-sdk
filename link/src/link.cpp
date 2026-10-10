#include <ebus/link/link.h>

#include <ebus/homie/Device.h>
#include <ebus/homie/Node.h>
#include <ebus/homie/Property.h>
#include <ebus/homie/controller.h>
#include <ebus/homie/homie.h>
#include <ebus/homie/homie_clock.h>
#include <ebus/homie/homie_log.h>
#include <ebus/homie/homie_settable.h>
#include <stdio.h>
#include <string.h>

static const char* const DEVICE_REF_FORM = "<node>/<property> or <device-id>/<node>/<property>";
static const char* const CONTROLLER_REF_FORM =
    "<device-id>/<node>/<property> (controller link)";

// Device::getNode() and Node::getProperty() log every miss; a link looks every interval.
static Property* find_in_node(Node* n, const char* prop_id) {
    for (int j = 0; j < n->numProperties(); j++) {
        Property* p = n->propertyAt(j);
        if (p && strcmp(p->id(), prop_id) == 0) return p;
    }
    return nullptr;
}

static Property* find_property(Device* d, const char* node_id, const char* prop_id) {
    if (!d) return nullptr;
    for (int i = 0; i < d->numNodes(); i++) {
        Node* n = d->nodeAt(i);
        if (n && strcmp(n->id(), node_id) == 0) return find_in_node(n, prop_id);
    }
    return nullptr;
}

// Copy a source value in, keeping the last good one when there is none. A controller's
// copy keeps the last text in value() after a retraction and clears has_value(); a local
// property's has_value() is false until its first reading.
static void take_value(EbusLinkSource& s, Property* prop) {
    if (ebus_link_take_value(s.value, sizeof(s.value), prop->value(), prop->has_value())) {
        s.has_value = true;
    }
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
            Property* pr = find_in_node(nd, prop);
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

// ── setup ──────────────────────────────────────────────────────────────────────────────

bool EbusLink::parse_target() {
    const char* form = _mode == CONTROLLER ? CONTROLLER_REF_FORM : DEVICE_REF_FORM;
    int min_parts = _mode == CONTROLLER ? 3 : 2;
    EbusLinkRef tgt;
    EbusLinkRefStatus st = ebus_link_parse_ref(_target, strlen(_target), &tgt);
    if (st != EBUS_LINK_REF_OK || tgt.parts < min_parts) {
        homie_logf("LINK[%s]: bad target '%s': %s; expected %s; link disabled\n", _id, _target,
                   st != EBUS_LINK_REF_OK ? ebus_link_ref_status_text(st) : "too few parts",
                   form);
        return false;
    }
    _target_remote = (tgt.parts == 3);
    int topic_len = 0;
    if (_target_remote) {
        snprintf(_tgt_dev, sizeof(_tgt_dev), "%s", tgt.dev);
        snprintf(_tgt_node, sizeof(_tgt_node), "%s", tgt.node);
        snprintf(_tgt_prop, sizeof(_tgt_prop), "%s", tgt.prop);
        _tgt_wild = tgt.wild;
        if (_tgt_wild && _mode != CONTROLLER) {
            homie_logf("LINK[%s]: target '%s' uses *, which needs the controller's discovery; "
                       "only a controller link resolves it; link disabled\n", _id, _target);
            return false;
        }
        // A pattern's path needs the real device id, so it is built when it binds.
        if (!_tgt_wild) {
            if (_mode == CONTROLLER) {
                topic_len = snprintf(_target_topic, sizeof(_target_topic), "%s/%s/%s",
                                     tgt.dev, tgt.node, tgt.prop);
                if (topic_len >= 0) topic_len += HOMIE_TOPIC_PREFIX_MAX + 1 + 4;
            } else {
                topic_len = snprintf(_target_topic, sizeof(_target_topic), "%s/%s/%s/%s/set",
                                     homie_topic_prefix(), tgt.dev, tgt.node, tgt.prop);
            }
        }
    } else {
        // Device::topic() is "<prefix>/<device-id>/", with the trailing slash.
        topic_len = snprintf(_target_topic, sizeof(_target_topic), "%s%s/%s/set",
                             _root->topic(), tgt.node, tgt.prop);
    }
    if (topic_len < 0 || topic_len > HOMIE_TOPIC_MAX) {
        homie_logf("LINK[%s]: target topic for '%s' is over %d chars; link disabled\n", _id,
                   _target, HOMIE_TOPIC_MAX);
        return false;
    }
    return true;
}

bool EbusLink::add_source(const char* ref, size_t len) {
    const char* form = _mode == CONTROLLER ? CONTROLLER_REF_FORM : DEVICE_REF_FORM;
    int min_parts = _mode == CONTROLLER ? 3 : 2;
    EbusLinkRef src;
    EbusLinkRefStatus st = ebus_link_parse_ref(ref, len, &src);
    if (st != EBUS_LINK_REF_OK || src.parts < min_parts || _num_sources == MAX_SOURCES) {
        homie_logf("LINK[%s]: bad source list '%s': %s; expected up to %d comma-separated %s; "
                   "link disabled\n", _id, _source,
                   st != EBUS_LINK_REF_OK
                       ? ebus_link_ref_status_text(st)
                       : (_num_sources == MAX_SOURCES ? "too many sources" : "too few parts"),
                   MAX_SOURCES, form);
        return false;
    }
    EbusLinkSource& s = _sources[_num_sources++];
    s.owner = this;
    s.remote = (src.parts == 3);
    if (!s.remote) {
        snprintf(s.node, sizeof(s.node), "%s", src.node);
        snprintf(s.prop, sizeof(s.prop), "%s", src.prop);
        _has_local = true;
        homie_logf("LINK[%s]: source %%%d = %s/%s (local, every %u ms)\n", _id, _num_sources,
                   s.node, s.prop, (unsigned)_interval_ms);
        return true;
    }
    snprintf(s.dev, sizeof(s.dev), "%s", src.dev);
    snprintf(s.node, sizeof(s.node), "%s", src.node);
    snprintf(s.prop, sizeof(s.prop), "%s", src.prop);
    s.wild = src.wild;
    if (_mode == CONTROLLER) {
        // The controller already subscribes <device>/+/+ for every device it discovers and
        // keeps the values; a second, exact subscription could make a broker deliver each
        // message twice. Read its cache instead.
        if (s.wild) {
            homie_logf("LINK[%s]: source %%%d = %s/%s/%s: pattern, binds once discovery "
                       "settles\n", _id, _num_sources, s.dev, s.node, s.prop);
        } else {
            homie_logf("LINK[%s]: source %%%d = %s/%s/%s (controller cache)\n", _id,
                       _num_sources, s.dev, s.node, s.prop);
        }
        return true;
    }
    if (s.wild) {
        homie_logf("LINK[%s]: source %%%d uses *, which needs the controller's discovery; only "
                   "a controller link resolves it; link disabled\n", _id, _num_sources);
        return false;
    }
    char topic[HOMIE_TOPIC_MAX + 1];
    int n = snprintf(topic, sizeof(topic), "%s/%s/%s/%s", homie_topic_prefix(), s.dev, s.node,
                     s.prop);
    if (n < 0 || n >= (int)sizeof(topic)) {
        homie_logf("LINK[%s]: source %%%d topic is over %d chars; link disabled\n", _id,
                   _num_sources, HOMIE_TOPIC_MAX);
        return false;
    }
    // The settable table copies the topic and the port re-subscribes it (QoS 0) on every
    // connect; settable_subscribe_now() covers a connection already up. It holds ONE
    // handler per topic, and a second registration would replace the first and leave the
    // earlier source without values for good, so a registered topic is refused.
    if (settable_is_registered(topic)) {
        homie_logf("LINK[%s]: source %%%d %s is already watched by another link source; one "
                   "topic can feed only one link source; link disabled\n", _id, _num_sources,
                   topic);
        return false;
    }
    subscribe_settable_handler(topic, &EbusLink::on_remote_value, &s);
    if (!settable_is_registered(topic)) {
        // The table is full (the core logged it): no value would ever arrive.
        homie_logf("LINK[%s]: source %%%d %s could not be registered; link disabled\n", _id,
                   _num_sources, topic);
        return false;
    }
    settable_subscribe_now(topic);
    homie_logf("LINK[%s]: source %%%d = %s (remote)\n", _id, _num_sources, topic);
    return true;
}

bool EbusLink::setup(Mode mode, Device* root) {
    _mode = mode;
    _root = root;
    if (_mode == DEVICE && !_root) {
        homie_logf("LINK[%s]: a device link needs the root device; link disabled\n", _id);
        return false;
    }
    if (!_source || !_target) {
        homie_logf("LINK[%s]: no source or no target; link disabled\n", _id);
        return false;
    }
    if (!parse_target()) return false;

    const char* p = _source;
    while (true) {
        size_t len = strcspn(p, ",");
        if (!add_source(p, len)) return false;
        p += len;
        if (*p == '\0') break;
        p++;
    }

    _ok = true;
    homie_logf("LINK[%s]: -> %s\n", _id, _tgt_wild ? _target : _target_topic);
    return true;
}

// ── running ────────────────────────────────────────────────────────────────────────────

// Run by settable_dispatch(), never inside the MQTT receive callback. It only stores;
// loop() renders and delivers. A retraction (empty payload) is not a new reading: keep
// the last good value. It is a watch, not a settable property of this device, so there
// is nothing to refuse and it returns true either way.
bool EbusLink::on_remote_value(void* ctx, const char* value) {
    EbusLinkSource* s = (EbusLinkSource*)ctx;
    if (!ebus_link_take_value(s->value, sizeof(s->value), value, true)) return true;
    s->has_value = true;
    s->owner->_remote_changed = true;
    return true;
}

void EbusLink::read_sources() {
    for (int i = 0; i < _num_sources; i++) {
        EbusLinkSource& s = _sources[i];
        if (_mode == CONTROLLER) {
            Property* prop = find_property(controller_get_device(s.dev), s.node, s.prop);
            if (prop) take_value(s, prop);
            continue;
        }
        if (s.remote) continue;
        Property* prop = find_property(_root, s.node, s.prop);
        if (!prop) {
            if (!_warned_missing) {
                homie_logf("LINK[%s]: source %s/%s not found on this device\n", _id, s.node,
                           s.prop);
                _warned_missing = true;
            }
            continue;
        }
        take_value(s, prop);
    }
}

bool EbusLink::deliver(const char* text) {
    // Counted in the controller's stats; non-retained, QoS 0 (Homie).
    if (_mode == CONTROLLER) return controller_set_property(_tgt_dev, _tgt_node, _tgt_prop, text);
    if (!_target_remote) return deliver_local_set(_target_topic, text);
    HomieTransport* t = _root->mqttClient();
    return t && t->publish(_target_topic, text, false, 0);
}

void EbusLink::loop() {
    if (!_ok) return;
    uint32_t now = homie_now_ms();

    if (_mode == CONTROLLER) {
        // Everything comes from the discovery cache, so it is polled on the interval. The
        // target's effective state gates delivery: /set is not retained, so a command to
        // a device that is not there is lost. Each time the target comes back to ready the
        // current text is sent again, so a display that rebooted catches up at once.
        if (now - _last_poll_ms < _interval_ms) return;
        _last_poll_ms = now;
        // A pattern that has not bound, or refused to, leaves the link idle.
        if (!resolve_wildcards()) return;

        bool ready = (controller_effective_state(_tgt_dev) == DEVICE_STATE_READY);
        if (ready && !_target_ready) {
            homie_logf("LINK[%s]: target %s is ready; sending current text\n", _id, _tgt_dev);
            _have_sent = false;
            _failing = false;   // a target that came back is tried at once
        }
        _target_ready = ready;
        read_sources();
        if (!ready) return;
    } else {
        // Remote values are acted on as soon as they arrive; the interval paces local
        // polling and retries of a failed delivery.
        bool due = (now - _last_poll_ms >= _interval_ms);
        if (!_remote_changed && !due) return;
        _remote_changed = false;
        if (due) _last_poll_ms = now;
        if (due && _has_local) read_sources();
    }

    for (int i = 0; i < _num_sources; i++) {
        if (!_sources[i].has_value) return;   // until every placeholder can be filled
    }

    const char* values[MAX_SOURCES];
    for (int i = 0; i < _num_sources; i++) values[i] = _sources[i].value;
    char text[TEXT_MAX];
    ebus_link_render(_format, values, _num_sources, _decimals, text, sizeof(text));
    if (_have_sent && strcmp(text, _sent) == 0) return;
    // Each attempt can log in the core, so a new value does not shorten the wait either.
    if (_failing && now - _failed_at_ms < _retry_wait_ms) return;

    if (deliver(text)) {
        snprintf(_sent, sizeof(_sent), "%s", text);
        _have_sent = true;
        if (_failing) homie_logf("LINK[%s]: delivered to %s\n", _id, _target_topic);
        _failing = false;
        _retry_wait_ms = 0;
        return;
    }
    if (!_failing) {
        homie_logf("LINK[%s]: delivery to %s failed; retrying, backing off from %u ms to %u "
                   "ms\n", _id, _target_topic, (unsigned)_interval_ms,
                   (unsigned)EBUS_LINK_RETRY_MAX_MS);
        _failing = true;
        _retry_wait_ms = 0;
    }
    _retry_wait_ms = ebus_link_next_retry_ms(_retry_wait_ms, _interval_ms);
    _failed_at_ms = now;
}
