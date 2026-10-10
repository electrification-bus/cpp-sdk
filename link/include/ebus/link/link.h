#pragma once
// A link copies up to three property values into one settable property: a sensor reading
// onto a display, a state onto a relay, a reading from one device onto another device.
// Behavior, reference forms and limits: doc/link.md.
//
//   source  up to MAX_SOURCES references, comma-separated, each one of
//             <node>/<property>              DEVICE mode: a property of the root device,
//                                            read directly
//             <device-id>/<node>/<property>  a property of another device: in DEVICE mode
//                                            watched on the broker through the settable
//                                            table, in CONTROLLER mode read from the
//                                            controller's discovery cache
//             */<node>/<property>            CONTROLLER mode: the one discovered device
//                                            that has that property; '*' may also appear
//                                            in the node or within the device id, never
//                                            in the property
//   target  <node>/<property>                DEVICE mode: a settable property of the root
//                                            device, set with deliver_local_set()
//           <device-id>/<node>/<property>    a settable property of another device: its
//                                            /set topic (DEVICE mode) or
//                                            controller_set_property() (CONTROLLER mode)
//
//   format       %1, %2, %3 become the sources' values in order, %s is %1, %% is '%'
//   decimals     round numeric values to this many places first; -1 sends them as is
//   interval_ms  how often local sources (and, in CONTROLLER mode, the cache) are read,
//                and the first retry delay after a failed delivery
#include <ebus/homie/homie_limits.h>
#include <ebus/link/link_core.h>
#include <stddef.h>
#include <stdint.h>

class Device;
class EbusLink;

// Makes EbusLink::CONTROLLER available: until it is called, setup(CONTROLLER) logs and
// returns false. Call it once before the first controller link's setup(). It is defined
// with the controller-mode code (link_controller.cpp), so a device build that never calls
// it links neither that code nor the controller and its discovery table.
void ebus_link_enable_controller();

// One source of an EbusLink. In DEVICE mode a remote source's value is written by the
// settable table's dispatch, which runs on the task that calls EbusLink::loop().
struct EbusLinkSource {
    EbusLink* owner = nullptr;
    bool remote = false;
    char dev[HOMIE_DEVICE_ID_MAX + 1] = {0};     // remote only
    char node[HOMIE_NODE_ID_MAX + 1] = {0};
    char prop[HOMIE_PROPERTY_ID_MAX + 1] = {0};
    char value[EBUS_LINK_VALUE_MAX] = {0};       // last good (non-empty) value
    bool has_value = false;
    // `dev` (and maybe `node`) is a * pattern not bound yet; both take the concrete ids
    // when it binds. `refused`: it matched more than one node and is never retried.
    bool wild = false;
    bool refused = false;
    bool unmatched = false;                      // matched nothing at the last attempt
};

class EbusLink {
 public:
    static const int MAX_SOURCES = 3;
    static const size_t TEXT_MAX = 128;          // rendered text, NUL included

    // DEVICE: the link runs in a device, whose root Device is passed to setup(). Two-part
    // references are that device's; a remote source is watched through the settable
    // table, and a remote target is published to its /set topic on the root's transport.
    // CONTROLLER: the link runs alongside the controller (controller_init()), after
    // ebus_link_enable_controller(). Every reference names a device; sources are read
    // from the discovery cache, '*' binds against it, and the target is commanded with
    // controller_set_property() only while it is ready.
    enum Mode { DEVICE, CONTROLLER };

    // The strings are not copied and must outlive the link. `id` names it in log lines.
    EbusLink(const char* id, const char* source, const char* target, const char* format = "%s",
             int decimals = -1, uint32_t interval_ms = 1000)
        : _id(id), _source(source), _target(target), _format(format ? format : "%s"),
          _decimals(decimals), _interval_ms(interval_ms) {}

    // Parse the references and, in DEVICE mode, register each remote source's watch.
    // DEVICE mode needs `root`; CONTROLLER mode ignores it and needs
    // ebus_link_enable_controller(). Returns false, having logged why, when the link is
    // disabled. Call once, from the task that runs the settable table; the link must not
    // move afterwards (the table holds pointers into it).
    bool setup(Mode mode, Device* root = nullptr);

    // Read, render and deliver. Call from the main loop: in DEVICE mode on the task that
    // calls settable_dispatch(), in CONTROLLER mode after controller_loop().
    void loop();

    const char* id() const { return _id; }
    bool enabled() const { return _ok; }
    Mode mode() const { return _mode; }
    int num_sources() const { return _num_sources; }
    const EbusLinkSource& source(int i) const { return _sources[i]; }
    // DEVICE mode: the target's /set topic. CONTROLLER mode: "<device>/<node>/<property>".
    // Empty while a target pattern is unbound.
    const char* target() const { return _target_topic; }
    bool target_bound() const { return !_tgt_wild && !_tgt_refused; }
    // The last text the target accepted, or null before the first.
    const char* last_sent() const { return _have_sent ? _sent : nullptr; }

 private:
    friend void ebus_link_enable_controller();
    // CONTROLLER mode's half of loop() and deliver(), in link_controller.cpp.
    struct ControllerOps {
        bool (EbusLink::*poll)(uint32_t now);
        bool (EbusLink::*deliver)(const char* text);
    };
    static const ControllerOps* _controller_ops;   // null until enabled

    static bool on_remote_value(void* ctx, const char* value);
    bool parse_target();
    bool add_source(const char* ref, size_t len);
    bool resolve_wildcards();
    void resolve_source(int i, bool remind, bool* said);
    bool resolve_target(bool remind, bool* said);
    bool controller_poll(uint32_t now);
    bool controller_deliver(const char* text);
    void read_sources();
    bool deliver(const char* text);

    const char* _id;
    const char* _source;
    const char* _target;
    const char* _format;
    int _decimals;
    uint32_t _interval_ms;

    Mode _mode = DEVICE;
    Device* _root = nullptr;
    bool _ok = false;
    EbusLinkSource _sources[MAX_SOURCES];
    int _num_sources = 0;
    bool _has_local = false;

    bool _target_remote = false;
    char _tgt_dev[HOMIE_DEVICE_ID_MAX + 1] = {0};
    char _tgt_node[HOMIE_NODE_ID_MAX + 1] = {0};
    char _tgt_prop[HOMIE_PROPERTY_ID_MAX + 1] = {0};
    char _target_topic[HOMIE_TOPIC_MAX + 1] = {0};
    bool _target_ready = false;
    bool _tgt_wild = false;
    bool _tgt_refused = false;
    bool _tgt_unmatched = false;
    // A wait or an unmatched pattern is logged when it starts, then at most every
    // EBUS_LINK_REMINDER_MS.
    bool _settle_logged = false;
    uint32_t _settle_logged_ms = 0;
    uint32_t _unmatched_said_ms = 0;

    bool _remote_changed = false;
    char _sent[TEXT_MAX] = {0};
    bool _have_sent = false;
    uint32_t _last_poll_ms = 0;
    bool _warned_missing = false;
    bool _failing = false;
    uint32_t _failed_at_ms = 0;
    uint32_t _retry_wait_ms = 0;
};
