#pragma once
// The pure functions behind a link (ebus/link/link.h): reference parsing, * matching,
// rounding and %-substitution. See doc/link.md.
#include <stddef.h>
#include <stdint.h>

#include <ebus/homie/homie_limits.h>

static const size_t EBUS_LINK_VALUE_MAX = 64;   // a source value, NUL included

// A failed delivery is retried after the poll interval, then after twice as long each
// time, up to EBUS_LINK_RETRY_MAX_MS. A condition that persists (an unmatched pattern, a wait
// for discovery) is logged when it starts and again at most every EBUS_LINK_REMINDER_MS.
static const uint32_t EBUS_LINK_RETRY_MAX_MS = 30000;
static const uint32_t EBUS_LINK_REMINDER_MS  = 60000;

// The wait before the next retry: `interval_ms` after the first failure (`current_ms` 0),
// then doubled each time, never over EBUS_LINK_RETRY_MAX_MS (nor under `interval_ms`).
uint32_t ebus_link_next_retry_ms(uint32_t current_ms, uint32_t interval_ms);

// One reference, "<node>/<property>" or "<device-id>/<node>/<property>", split into
// Homie-legal parts. A part without '*' is sanitized as sanitize_homie_id() does; a part
// with one is a pattern and keeps it.
struct EbusLinkRef {
    int  parts = 0;                              // 2 or 3
    bool wild  = false;                          // the device or node part is a pattern
    char dev[HOMIE_DEVICE_ID_MAX + 1]    = {0};  // 3 parts only
    char node[HOMIE_NODE_ID_MAX + 1]     = {0};
    char prop[HOMIE_PROPERTY_ID_MAX + 1] = {0};
};

enum EbusLinkRefStatus {
    EBUS_LINK_REF_OK = 0,
    EBUS_LINK_REF_SHAPE,              // empty, an empty part, or not two or three parts
    EBUS_LINK_REF_TOO_LONG,           // a part over HOMIE_DEVICE_ID_MAX / _NODE_ / _PROPERTY_
    EBUS_LINK_REF_BAD_PATTERN,        // a pattern with a character other than a-z 0-9 - *
    EBUS_LINK_REF_PROPERTY_PATTERN,   // '*' in the property, which is never resolved
};

// Parse exactly `len` bytes of `ref`, surrounding spaces ignored. `out` is filled only on
// EBUS_LINK_REF_OK.
EbusLinkRefStatus ebus_link_parse_ref(const char* ref, size_t len, EbusLinkRef* out);

// What a status means, for a log line.
const char* ebus_link_ref_status_text(EbusLinkRefStatus status);

// Does `s` match `pat`, where '*' in `pat` stands for any run of characters (including
// none)?
bool ebus_link_glob_match(const char* pat, const char* s);

// Binding a * reference: every discovered device/node pair that matches the reference's
// device and node patterns and has the property is passed to ebus_link_resolve_add(), and the
// reference binds only if exactly one did.
enum EbusLinkResolveResult { EBUS_LINK_RESOLVE_NONE, EBUS_LINK_RESOLVE_ONE, EBUS_LINK_RESOLVE_MANY };

struct EbusLinkResolve {
    int  hits = 0;
    char dev[HOMIE_DEVICE_ID_MAX + 1] = {0};    // the first match
    char node[HOMIE_NODE_ID_MAX + 1]  = {0};
    char cands[192] = {0};                      // "dev/node, dev/node", for one log line
};

// Does the discovered `dev_id`/`node_id` match the patterns? A part without '*' must be
// equal.
bool ebus_link_ref_matches(const char* dev_pat, const char* node_pat, const char* dev_id,
                      const char* node_id);
void ebus_link_resolve_add(EbusLinkResolve* r, const char* dev_id, const char* node_id);
EbusLinkResolveResult ebus_link_resolve_result(const EbusLinkResolve* r);

// Store a source's new value in `dst` (`dst_size` bytes, cut on a UTF-8 boundary), unless
// there is none: `present` false (a controller's copy whose retained value was removed) or
// an empty `value` (a retraction, or no reading yet). Returns whether it stored, so the
// caller keeps the last good value otherwise.
bool ebus_link_take_value(char* dst, size_t dst_size, const char* value, bool present);

// `value` rounded to `decimals` places (at most 6) if it is a plain decimal number under
// 1e15 in magnitude, else copied as is. A negative `decimals` copies every value as is.
void ebus_link_rounded(const char* value, int decimals, char* out, size_t out_len);

// `format` with %1..%3 replaced by values[0..2] (each through ebus_link_rounded()), %s by
// values[0] and %% by '%'. A placeholder past `num_values` is left as written.
void ebus_link_render(const char* format, const char* const* values, int num_values, int decimals,
                 char* out, size_t out_len);
