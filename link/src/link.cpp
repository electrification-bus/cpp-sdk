#include <ebus/link/link.h>

#include <ebus/homie/Device.h>
#include <ebus/homie/homie.h>
#include <ebus/homie/homie_clock.h>
#include <ebus/homie/homie_log.h>
#include <ebus/homie/homie_settable.h>
#include <stdio.h>
#include <string.h>

#include "link_internal.h"

// Controller mode's binding, cache reads and delivery are in link_controller.cpp, reached
// through this table, which ebus_link_enable_controller() installs: a build that never
// calls it links none of the controller.
const EbusLink::ControllerOps* EbusLink::_controller_ops = nullptr;

static const char* const DEVICE_REF_FORM = "<node>/<property> or <device-id>/<node>/<property>";
static const char* const CONTROLLER_REF_FORM =
    "<device-id>/<node>/<property> (controller link)";

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
    if (_mode == CONTROLLER && !_controller_ops) {
        homie_logf("LINK[%s]: a controller link needs ebus_link_enable_controller() first; "
                   "link disabled\n", _id);
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
        if (s.remote) continue;
        Property* prop = link_find_property(_root, s.node, s.prop);
        if (!prop) {
            if (!_warned_missing) {
                homie_logf("LINK[%s]: source %s/%s not found on this device\n", _id, s.node,
                           s.prop);
                _warned_missing = true;
            }
            continue;
        }
        link_take_value(s, prop);
    }
}

bool EbusLink::deliver(const char* text) {
    if (_mode == CONTROLLER) return (this->*_controller_ops->deliver)(text);
    if (!_target_remote) return deliver_local_set(_target_topic, text);
    HomieTransport* t = _root->mqttClient();
    return t && t->publish(_target_topic, text, false, 0);
}

void EbusLink::loop() {
    if (!_ok) return;
    uint32_t now = homie_now_ms();

    if (_mode == CONTROLLER) {
        if (!(this->*_controller_ops->poll)(now)) return;
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
