#include <ebus/link/link_core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ebus/homie/homie_id.h>
#include <ebus/link/utf8.h>

// A pattern part keeps '*' and must already be Homie-legal otherwise (A-Z is lowercased).
// Returns false on any other character, or when it does not fit `out_size`.
static bool copy_pattern(const char* in, size_t len, char* out, size_t out_size,
                         EbusLinkRefStatus* status) {
    if (len >= out_size) { *status = EBUS_LINK_REF_TOO_LONG; return false; }
    for (size_t i = 0; i < len; i++) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '*')) {
            *status = EBUS_LINK_REF_BAD_PATTERN;
            return false;
        }
        out[i] = c;
    }
    out[len] = '\0';
    return true;
}

// One part into `out` (`out_size` bytes, NUL included): a pattern as copy_pattern(), any
// other through sanitize_homie_id(). Sets *status and returns false when it does not fit,
// is a malformed pattern, or sanitizes to nothing.
static bool take_part(const char* in, size_t len, char* out, size_t out_size,
                      EbusLinkRefStatus* status) {
    if (memchr(in, '*', len)) return copy_pattern(in, len, out, out_size, status);
    // sanitize_homie_id() needs a NUL-terminated input. Anything longer than the largest
    // part is too long, whatever it sanitizes to.
    char raw[HOMIE_DEVICE_ID_MAX * 2 + 2];
    if (len >= sizeof(raw)) { *status = EBUS_LINK_REF_TOO_LONG; return false; }
    memcpy(raw, in, len);
    raw[len] = '\0';
    if (!sanitize_homie_id(raw, out, out_size)) { *status = EBUS_LINK_REF_TOO_LONG; return false; }
    if (out[0] == '\0') { *status = EBUS_LINK_REF_SHAPE; return false; }
    return true;
}

EbusLinkRefStatus ebus_link_parse_ref(const char* ref, size_t len, EbusLinkRef* out) {
    while (len && *ref == ' ') { ref++; len--; }
    while (len && ref[len - 1] == ' ') len--;
    if (len == 0) return EBUS_LINK_REF_SHAPE;

    // Up to three parts, by position.
    const char* seg[3];
    size_t seg_len[3];
    int n = 0;
    const char* p   = ref;
    const char* end = ref + len;
    while (true) {
        const char* slash = (const char*)memchr(p, '/', end - p);
        size_t l = (slash ? slash : end) - p;
        if (l == 0 || n == 3) return EBUS_LINK_REF_SHAPE;
        seg[n] = p;
        seg_len[n] = l;
        n++;
        if (!slash) break;
        p = slash + 1;
    }
    if (n < 2) return EBUS_LINK_REF_SHAPE;

    const char* prop = seg[n - 1];
    size_t prop_len  = seg_len[n - 1];
    if (memchr(prop, '*', prop_len)) return EBUS_LINK_REF_PROPERTY_PATTERN;

    EbusLinkRef r;
    EbusLinkRefStatus status = EBUS_LINK_REF_OK;
    r.parts = n;
    if (n == 3 && !take_part(seg[0], seg_len[0], r.dev, sizeof(r.dev), &status)) return status;
    if (!take_part(seg[n - 2], seg_len[n - 2], r.node, sizeof(r.node), &status)) return status;
    if (!take_part(prop, prop_len, r.prop, sizeof(r.prop), &status)) return status;
    r.wild = strchr(r.dev, '*') || strchr(r.node, '*');
    *out = r;
    return EBUS_LINK_REF_OK;
}

#define EBUS_LINK_STR_(x) #x
#define EBUS_LINK_STR(x)  EBUS_LINK_STR_(x)

const char* ebus_link_ref_status_text(EbusLinkRefStatus status) {
    switch (status) {
        case EBUS_LINK_REF_OK:                 return "ok";
        case EBUS_LINK_REF_SHAPE:              return "not two or three non-empty parts";
        case EBUS_LINK_REF_TOO_LONG:           return "a part is longer than the core holds "
                                                    "(device " EBUS_LINK_STR(HOMIE_DEVICE_ID_MAX)
                                                    ", node " EBUS_LINK_STR(HOMIE_NODE_ID_MAX)
                                                    ", property " EBUS_LINK_STR(HOMIE_PROPERTY_ID_MAX)
                                                    " chars)";
        case EBUS_LINK_REF_BAD_PATTERN:        return "a * pattern may hold only a-z, 0-9, - and *";
        case EBUS_LINK_REF_PROPERTY_PATTERN:   return "* in the property is never resolved";
    }
    return "?";
}

// Iterative with a backtrack point, so no recursion on a long id.
bool ebus_link_glob_match(const char* pat, const char* s) {
    const char* star = nullptr;
    const char* back = nullptr;
    while (*s) {
        if (*pat == '*')            { star = ++pat; back = s; }
        else if (*pat == *s)        { pat++; s++; }
        else if (star)              { pat = star; s = ++back; }
        else                        return false;
    }
    while (*pat == '*') pat++;
    return *pat == '\0';
}

uint32_t ebus_link_next_retry_ms(uint32_t current_ms, uint32_t interval_ms) {
    uint32_t next = current_ms == 0 ? interval_ms
                  : (current_ms > EBUS_LINK_RETRY_MAX_MS / 2 ? EBUS_LINK_RETRY_MAX_MS : current_ms * 2);
    if (next > EBUS_LINK_RETRY_MAX_MS) next = EBUS_LINK_RETRY_MAX_MS;
    return next < interval_ms ? interval_ms : next;
}

bool ebus_link_ref_matches(const char* dev_pat, const char* node_pat, const char* dev_id,
                      const char* node_id) {
    return ebus_link_glob_match(dev_pat, dev_id) && ebus_link_glob_match(node_pat, node_id);
}

void ebus_link_resolve_add(EbusLinkResolve* r, const char* dev_id, const char* node_id) {
    if (++r->hits == 1) {
        snprintf(r->dev, sizeof(r->dev), "%s", dev_id);
        snprintf(r->node, sizeof(r->node), "%s", node_id);
    }
    size_t used = strlen(r->cands);
    snprintf(r->cands + used, sizeof(r->cands) - used, "%s%s/%s", used ? ", " : "",
             dev_id, node_id);
}

EbusLinkResolveResult ebus_link_resolve_result(const EbusLinkResolve* r) {
    if (r->hits == 0) return EBUS_LINK_RESOLVE_NONE;
    return r->hits == 1 ? EBUS_LINK_RESOLVE_ONE : EBUS_LINK_RESOLVE_MANY;
}

bool ebus_link_take_value(char* dst, size_t dst_size, const char* value, bool present) {
    if (!present || !value || value[0] == '\0') return false;
    ebus_utf8_copy(dst, dst_size, value);
    return true;
}

// Optional sign, digits with an optional fraction (at least one digit in all), and an
// optional exponent of one or two digits. Hex, "nan", "inf" and surrounding spaces, all of
// which strtod() would take, are not.
static bool plain_decimal(const char* s) {
    const char* p = s;
    if (*p == '+' || *p == '-') p++;
    int digits = 0;
    while (*p >= '0' && *p <= '9') { p++; digits++; }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') { p++; digits++; }
    }
    if (digits == 0) return false;
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        int exp_digits = 0;
        while (*p >= '0' && *p <= '9') { p++; exp_digits++; }
        if (exp_digits == 0 || exp_digits > 2) return false;
    }
    return *p == '\0';
}

// Below this a double still holds every integer digit, and "%.6f" of it is at most
// 23 characters.
static const double ROUND_MAX = 1e15;

void ebus_link_rounded(const char* value, int decimals, char* out, size_t out_len) {
    ebus_utf8_copy(out, out_len, value);
    if (decimals < 0 || !plain_decimal(value)) return;
    double v = strtod(value, nullptr);
    if (v >= ROUND_MAX || v <= -ROUND_MAX) return;
    char num[32];
    int n = snprintf(num, sizeof(num), "%.*f", decimals > 6 ? 6 : decimals, v);
    if (n < 0 || (size_t)n >= sizeof(num) || (size_t)n >= out_len) return;
    // -0.04 at one place prints "-0.0"; a reading of zero should not carry a sign.
    const char* shown = (num[0] == '-' && strtod(num, nullptr) == 0.0) ? num + 1 : num;
    memcpy(out, shown, strlen(shown) + 1);
}

// Append the first `len` bytes of `s` at out[*o], as much as fits on a character boundary.
// Returns false once something did not fit.
static bool append(char* out, size_t out_len, size_t* o, const char* s, size_t len) {
    size_t room = out_len - 1 - *o;
    size_t n = ebus_utf8_fit(s, len, room);
    memcpy(out + *o, s, n);
    *o += n;
    return n == len;
}

// By hand: the format comes from configuration and must not be able to reach printf with a
// conversion it did not mean. Text that does not fit is cut on a character boundary.
void ebus_link_render(const char* format, const char* const* values, int num_values, int decimals,
                 char* out, size_t out_len) {
    if (out_len == 0) return;
    size_t o = 0;
    const char* f = format;
    while (*f) {
        int idx = -1;
        if (f[0] == '%' && f[1] == 's') idx = 0;
        else if (f[0] == '%' && f[1] >= '1' && f[1] < '1' + num_values) idx = f[1] - '1';

        bool fits;
        if (idx >= 0) {
            char shown[EBUS_LINK_VALUE_MAX];
            ebus_link_rounded(values[idx], decimals, shown, sizeof(shown));
            fits = append(out, out_len, &o, shown, strlen(shown));
            f += 2;
        } else if (f[0] == '%' && f[1] == '%') {
            fits = append(out, out_len, &o, "%", 1);
            f += 2;
        } else {
            // A run of literal text up to the next '%' (a '%' that starts no token is
            // literal too, and begins the next run).
            const char* next = strchr(f + 1, '%');
            size_t len = next ? (size_t)(next - f) : strlen(f);
            fits = append(out, out_len, &o, f, len);
            f += len;
        }
        if (!fits) break;
    }
    out[o] = '\0';
}
