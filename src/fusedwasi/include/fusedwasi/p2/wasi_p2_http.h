/* wasi:http (63 imports): types (62) and outgoing-handler (1) (the twin of p2/wasi_p2_http.py, where the rules and the layouts are described). `incoming-handler` is an
 * export of a component, not an import.
 *
 * An outgoing request is refused (`HTTP-request-denied`) unless `g->http_check` says yes. The transport of the host is a small HTTP/1.1 client over a socket (`Connection: close`);
 * plain `http` only (BSD sockets, or Winsock on Windows): `https` is `internal-error` unless the caller sets `state->http_send` (the transport is then the caller's, with TLS if it likes). The answer is read
 * whole before the guest sees it, and a request is sent when the guest first asks for the answer. Objects that are shared (fields, bodies, responses, the answer to a future) are
 * counted; the embedder of a server side (`fusedwasi_p2_http_incoming_request_new`, `fusedwasi_p2_http_outparam_new`) holds references of its own and releases them. */
#ifndef FUSEDWASI_P2_HTTP_H
#define FUSEDWASI_P2_HTTP_H

#include "wasi_p2_net.h"
#include <limits.h>

#include <errno.h>

#include "wasi_p2_io.h"

#define FUSEDWASI_P2_HTTP_MAX_BODY (1ULL << 28)
#define FUSEDWASI_P2_HTTP_MAX_HEAD 65536u
#define FUSEDWASI_P2_HTTP_DEFAULT_TIMEOUT_NS 600000000000ULL

/* error-code, in the order of the WIT */
enum {
    FUSEDWASI_P2_HTTP_DNS_TIMEOUT, FUSEDWASI_P2_HTTP_DNS_ERROR, FUSEDWASI_P2_HTTP_DESTINATION_NOT_FOUND, FUSEDWASI_P2_HTTP_DESTINATION_UNAVAILABLE,
    FUSEDWASI_P2_HTTP_DESTINATION_IP_PROHIBITED, FUSEDWASI_P2_HTTP_DESTINATION_IP_UNROUTABLE, FUSEDWASI_P2_HTTP_CONNECTION_REFUSED, FUSEDWASI_P2_HTTP_CONNECTION_TERMINATED,
    FUSEDWASI_P2_HTTP_CONNECTION_TIMEOUT, FUSEDWASI_P2_HTTP_CONNECTION_READ_TIMEOUT, FUSEDWASI_P2_HTTP_CONNECTION_WRITE_TIMEOUT, FUSEDWASI_P2_HTTP_CONNECTION_LIMIT_REACHED,
    FUSEDWASI_P2_HTTP_TLS_PROTOCOL_ERROR, FUSEDWASI_P2_HTTP_TLS_CERTIFICATE_ERROR, FUSEDWASI_P2_HTTP_TLS_ALERT_RECEIVED, FUSEDWASI_P2_HTTP_REQUEST_DENIED,
    FUSEDWASI_P2_HTTP_REQUEST_LENGTH_REQUIRED, FUSEDWASI_P2_HTTP_REQUEST_BODY_SIZE, FUSEDWASI_P2_HTTP_REQUEST_METHOD_INVALID, FUSEDWASI_P2_HTTP_REQUEST_URI_INVALID,
    FUSEDWASI_P2_HTTP_REQUEST_URI_TOO_LONG, FUSEDWASI_P2_HTTP_REQUEST_HEADER_SECTION_SIZE, FUSEDWASI_P2_HTTP_REQUEST_HEADER_SIZE, FUSEDWASI_P2_HTTP_REQUEST_TRAILER_SECTION_SIZE,
    FUSEDWASI_P2_HTTP_REQUEST_TRAILER_SIZE, FUSEDWASI_P2_HTTP_RESPONSE_INCOMPLETE, FUSEDWASI_P2_HTTP_RESPONSE_HEADER_SECTION_SIZE, FUSEDWASI_P2_HTTP_RESPONSE_HEADER_SIZE,
    FUSEDWASI_P2_HTTP_RESPONSE_BODY_SIZE, FUSEDWASI_P2_HTTP_RESPONSE_TRAILER_SECTION_SIZE, FUSEDWASI_P2_HTTP_RESPONSE_TRAILER_SIZE, FUSEDWASI_P2_HTTP_RESPONSE_TRANSFER_CODING,
    FUSEDWASI_P2_HTTP_RESPONSE_CONTENT_CODING, FUSEDWASI_P2_HTTP_RESPONSE_TIMEOUT, FUSEDWASI_P2_HTTP_UPGRADE_FAILED, FUSEDWASI_P2_HTTP_PROTOCOL_ERROR, FUSEDWASI_P2_HTTP_LOOP_DETECTED,
    FUSEDWASI_P2_HTTP_CONFIGURATION_ERROR, FUSEDWASI_P2_HTTP_INTERNAL_ERROR, FUSEDWASI_P2_HTTP_CASES
};

/* the shape of the payload of each case: the leaves are options of a number or a string; `outer`: the payload is in an option */
enum { FUSEDWASI_P2_HK_NONE, FUSEDWASI_P2_HK_N8, FUSEDWASI_P2_HK_N16, FUSEDWASI_P2_HK_N32, FUSEDWASI_P2_HK_N64, FUSEDWASI_P2_HK_STR };

typedef struct {
    uint8_t outer, leaves, kind[2];
} fusedwasi_p2_http_case;

FUSEDWASI_P2_FN const fusedwasi_p2_http_case *fusedwasi_p2_http_case_of(uint32_t tag) {
    static const fusedwasi_p2_http_case table[FUSEDWASI_P2_HTTP_CASES] = {
        /* 0 DNS-timeout */ {0, 0, {0, 0}},
        /* 1 DNS-error */ {0, 2, {FUSEDWASI_P2_HK_STR, FUSEDWASI_P2_HK_N16}},
        {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}},
        /* 12 TLS-protocol-error, 13 TLS-certificate-error */ {0, 0, {0, 0}}, {0, 0, {0, 0}},
        /* 14 TLS-alert-received */ {0, 2, {FUSEDWASI_P2_HK_N8, FUSEDWASI_P2_HK_STR}},
        /* 15 HTTP-request-denied, 16 length-required */ {0, 0, {0, 0}}, {0, 0, {0, 0}},
        /* 17 HTTP-request-body-size */ {0, 1, {FUSEDWASI_P2_HK_N64, 0}},
        /* 18 method-invalid, 19 URI-invalid, 20 URI-too-long */ {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}},
        /* 21 HTTP-request-header-section-size */ {0, 1, {FUSEDWASI_P2_HK_N32, 0}},
        /* 22 HTTP-request-header-size */ {1, 2, {FUSEDWASI_P2_HK_STR, FUSEDWASI_P2_HK_N32}},
        /* 23 HTTP-request-trailer-section-size */ {0, 1, {FUSEDWASI_P2_HK_N32, 0}},
        /* 24 HTTP-request-trailer-size */ {0, 2, {FUSEDWASI_P2_HK_STR, FUSEDWASI_P2_HK_N32}},
        /* 25 HTTP-response-incomplete */ {0, 0, {0, 0}},
        /* 26 HTTP-response-header-section-size */ {0, 1, {FUSEDWASI_P2_HK_N32, 0}},
        /* 27 HTTP-response-header-size */ {0, 2, {FUSEDWASI_P2_HK_STR, FUSEDWASI_P2_HK_N32}},
        /* 28 HTTP-response-body-size */ {0, 1, {FUSEDWASI_P2_HK_N64, 0}},
        /* 29 HTTP-response-trailer-section-size */ {0, 1, {FUSEDWASI_P2_HK_N32, 0}},
        /* 30 HTTP-response-trailer-size */ {0, 2, {FUSEDWASI_P2_HK_STR, FUSEDWASI_P2_HK_N32}},
        /* 31 transfer-coding, 32 content-coding */ {0, 1, {FUSEDWASI_P2_HK_STR, 0}}, {0, 1, {FUSEDWASI_P2_HK_STR, 0}},
        /* 33 HTTP-response-timeout, 34 upgrade-failed, 35 protocol-error, 36 loop-detected, 37 configuration-error */
        {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}}, {0, 0, {0, 0}},
        /* 38 internal-error */ {0, 1, {FUSEDWASI_P2_HK_STR, 0}},
    };
    return &table[tag];
}

/* an error-code: the tag and the leaves of its payload (`has`: the option is some); strings are owned (fusedwasi_p2_http_error_free) */
typedef struct fusedwasi_p2_http_error {
    uint32_t tag;
    int present; /* HTTP-request-header-size: the option around the record */
    int has[2];
    uint64_t num[2];
    char *str[2];
    uint32_t len[2];
} fusedwasi_p2_http_error;

FUSEDWASI_P2_FN void fusedwasi_p2_http_error_free(fusedwasi_p2_http_error *e) {
    for (int i = 0; i < 2; i++) free(e->str[i]), e->str[i] = NULL;
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_error_init(fusedwasi_p2_http_error *e, uint32_t tag) {
    memset(e, 0, sizeof *e);
    e->tag = tag;
    e->present = 1;
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_error_set_str(fusedwasi_p2_http_error *e, int leaf, const char *text, size_t n) {
    free(e->str[leaf]);
    e->str[leaf] = (char *)malloc(n + 1);
    if (e->str[leaf]) {
        memcpy(e->str[leaf], text, n);
        e->str[leaf][n] = 0;
    }
    e->len[leaf] = (uint32_t)n;
    e->has[leaf] = e->str[leaf] != NULL;
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_error_set_num(fusedwasi_p2_http_error *e, int leaf, uint64_t v) {
    e->num[leaf] = v;
    e->has[leaf] = 1;
}

/* the helpers that the transport uses to fail: they return 1 */
FUSEDWASI_P2_FN int fusedwasi_p2_http_fail(fusedwasi_p2_http_error *e, uint32_t tag) {
    fusedwasi_p2_http_error_free(e);
    fusedwasi_p2_http_error_init(e, tag);
    return 1;
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_fail_str(fusedwasi_p2_http_error *e, uint32_t tag, int leaf, const char *text) {
    fusedwasi_p2_http_fail(e, tag);
    fusedwasi_p2_http_error_set_str(e, leaf, text, strlen(text));
    return 1;
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_fail_num(fusedwasi_p2_http_error *e, uint32_t tag, uint64_t v) {
    fusedwasi_p2_http_fail(e, tag);
    fusedwasi_p2_http_error_set_num(e, 0, v);
    return 1;
}

/* writing and reading an error-code in memory: 32 bytes aligned to 8, the tag at 0, the payload at 8 */

FUSEDWASI_P2_FN void fusedwasi_p2_http_store_string(fusedwasi_p2_guest *g, uint32_t comp, uint32_t at, const void *data, uint32_t n) {
    uint32_t ptr = fusedwasi_p2_alloc(g, comp, 1, n);
    if (n) memcpy(fusedwasi_p2_mem(g, comp, ptr, n), data, n);
    fusedwasi_p2_store_u32(g, comp, at, ptr);
    fusedwasi_p2_store_u32(g, comp, at + 4, n);
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_store_opt_string(fusedwasi_p2_guest *g, uint32_t comp, uint32_t at, const char *text) {
    fusedwasi_p2_store_u8(g, comp, at, text ? 1 : 0);
    if (text) fusedwasi_p2_http_store_string(g, comp, at + 4, text, (uint32_t)strlen(text));
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_leaf(int kind, uint32_t *size, uint32_t *align) {
    switch (kind) {
    case FUSEDWASI_P2_HK_N8: *size = 2, *align = 1; break;
    case FUSEDWASI_P2_HK_N16: *size = 4, *align = 2; break;
    case FUSEDWASI_P2_HK_N32: *size = 8, *align = 4; break;
    case FUSEDWASI_P2_HK_N64: *size = 16, *align = 8; break;
    default: *size = 12, *align = 4; break;
    }
}

/* where the leaves are, from the start of the payload */
FUSEDWASI_P2_FN void fusedwasi_p2_http_offsets(const fusedwasi_p2_http_case *c, uint32_t offs[2]) {
    uint32_t pos = 0, size, align, max_align = 1;
    for (int i = 0; i < c->leaves; i++) {
        fusedwasi_p2_http_leaf(c->kind[i], &size, &align);
        if (align > max_align) max_align = align;
    }
    if (c->outer) pos = (1 + max_align - 1) / max_align * max_align;
    for (int i = 0; i < c->leaves; i++) {
        fusedwasi_p2_http_leaf(c->kind[i], &size, &align);
        pos = (pos + align - 1) / align * align;
        offs[i] = pos;
        pos += size;
    }
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_store_error(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr, const fusedwasi_p2_http_error *e) {
    const fusedwasi_p2_http_case *c = fusedwasi_p2_http_case_of(e->tag);
    uint32_t offs[2] = {0, 0};
    fusedwasi_p2_store_u8(g, comp, ptr, (uint8_t)e->tag);
    uint32_t base = ptr + 8;
    if (c->outer) {
        fusedwasi_p2_store_u8(g, comp, base, e->present ? 1 : 0);
        if (!e->present) return;
    }
    fusedwasi_p2_http_offsets(c, offs);
    for (int i = 0; i < c->leaves; i++) {
        uint32_t at = base + offs[i], size, align;
        fusedwasi_p2_http_leaf(c->kind[i], &size, &align);
        if (c->kind[i] == FUSEDWASI_P2_HK_STR) {
            fusedwasi_p2_store_u8(g, comp, at, e->has[i] ? 1 : 0);
            if (e->has[i]) fusedwasi_p2_http_store_string(g, comp, at + 4, e->str[i], e->len[i]);
            continue;
        }
        fusedwasi_p2_store_u8(g, comp, at, e->has[i] ? 1 : 0);
        if (e->has[i]) {
            uint8_t *p = fusedwasi_p2_mem(g, comp, at + align, align);
            for (uint32_t b = 0; b < align; b++) p[b] = (uint8_t)(e->num[i] >> (8 * b));
        }
    }
}

/* an error-code that came as flat arguments: the tag, then the numbers of the payload (a string is a pointer and a length); strings are copied */
FUSEDWASI_P2_FN void fusedwasi_p2_http_lift_error(fusedwasi_p2_guest *g, uint32_t comp, const uint64_t *flat, fusedwasi_p2_http_error *e) {
    if (flat[0] >= FUSEDWASI_P2_HTTP_CASES) g->trap(g, "bad error-code");
    const fusedwasi_p2_http_case *c = fusedwasi_p2_http_case_of((uint32_t)flat[0]);
    fusedwasi_p2_http_error_init(e, (uint32_t)flat[0]);
    int pos = 1;
    if (c->outer) e->present = (flat[pos++] & 0xFF) != 0;
    for (int i = 0; i < c->leaves && e->present; i++) {
        int has = (flat[pos] & 0xFF) != 0;
        if (c->kind[i] == FUSEDWASI_P2_HK_STR) {
            if (has) {
                uint32_t ptr = (uint32_t)flat[pos + 1], n = (uint32_t)flat[pos + 2];
                fusedwasi_p2_http_error_set_str(e, i, (const char *)fusedwasi_p2_mem(g, comp, ptr, n), n);
            }
            pos += 3;
        } else {
            uint32_t size, align;
            fusedwasi_p2_http_leaf(c->kind[i], &size, &align);
            if (has) fusedwasi_p2_http_error_set_num(e, i, align == 8 ? flat[pos + 1] : flat[pos + 1] & ((1ULL << (8 * align)) - 1));
            pos += 2;
        }
    }
}

/* fields */

typedef struct {
    char *name; /* lower case */
    uint8_t *value;
    uint32_t len;
} fusedwasi_p2_http_entry;

typedef struct fusedwasi_p2_http_fields {
    int refs, immutable;
    fusedwasi_p2_http_entry *e;
    uint32_t count, capacity;
} fusedwasi_p2_http_fields;

FUSEDWASI_P2_FN fusedwasi_p2_http_fields *fusedwasi_p2_http_fields_new(int immutable) {
    fusedwasi_p2_http_fields *f = (fusedwasi_p2_http_fields *)calloc(1, sizeof *f);
    if (f) f->refs = 1, f->immutable = immutable;
    return f;
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_fields_ref(fusedwasi_p2_http_fields *f) {
    if (f) f->refs++;
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_fields_unref(fusedwasi_p2_http_fields *f) {
    if (!f || --f->refs > 0) return;
    for (uint32_t i = 0; i < f->count; i++) free(f->e[i].name), free(f->e[i].value);
    free(f->e);
    free(f);
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* appends (name, value); the name is made lower case. Returns 0, or -1 if out of memory. */
FUSEDWASI_P2_FN int fusedwasi_p2_http_fields_add(fusedwasi_p2_http_fields *f, const void *name, size_t nlen, const void *value, size_t vlen) {
    if (f->count == f->capacity) {
        uint32_t cap = f->capacity ? f->capacity * 2 : 8;
        fusedwasi_p2_http_entry *grown = (fusedwasi_p2_http_entry *)realloc(f->e, cap * sizeof *grown);
        if (!grown) return -1;
        f->e = grown;
        f->capacity = cap;
    }
    char *n = (char *)malloc(nlen + 1);
    uint8_t *v = (uint8_t *)malloc(vlen + 1);
    if (!n || !v) {
        free(n), free(v);
        return -1;
    }
    for (size_t i = 0; i < nlen; i++) n[i] = (char)fusedwasi_p2_http_lower(((const unsigned char *)name)[i]);
    n[nlen] = 0;
    if (vlen) memcpy(v, value, vlen);
    v[vlen] = 0;
    f->e[f->count].name = n;
    f->e[f->count].value = v;
    f->e[f->count].len = (uint32_t)vlen;
    f->count++;
    return 0;
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_name_is(const fusedwasi_p2_http_entry *e, const uint8_t *name, size_t nlen) {
    if (strlen(e->name) != nlen) return 0;
    for (size_t i = 0; i < nlen; i++)
        if (e->name[i] != fusedwasi_p2_http_lower(name[i])) return 0;
    return 1;
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_fields_remove(fusedwasi_p2_http_fields *f, const uint8_t *name, size_t nlen) {
    uint32_t out = 0;
    for (uint32_t i = 0; i < f->count; i++) {
        if (fusedwasi_p2_http_name_is(&f->e[i], name, nlen)) {
            free(f->e[i].name), free(f->e[i].value);
        } else {
            f->e[out++] = f->e[i];
        }
    }
    f->count = out;
}

/* the first value of a field (NUL-terminated, for the transport), or NULL */
FUSEDWASI_P2_FN const fusedwasi_p2_http_entry *fusedwasi_p2_http_fields_find(const fusedwasi_p2_http_fields *f, const char *name) {
    for (uint32_t i = 0; f && i < f->count; i++)
        if (fusedwasi_p2_http_name_is(&f->e[i], (const uint8_t *)name, strlen(name))) return &f->e[i];
    return NULL;
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_token_char(int c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c && strchr("!#$%&'*+-.^_`|~", c) != NULL);
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_is_token(const uint8_t *p, size_t n) {
    if (n == 0) return 0;
    for (size_t i = 0; i < n; i++)
        if (!fusedwasi_p2_http_token_char(p[i])) return 0;
    return 1;
}

enum { FUSEDWASI_P2_HE_NONE = -1, FUSEDWASI_P2_HE_INVALID_SYNTAX = 0, FUSEDWASI_P2_HE_FORBIDDEN = 1, FUSEDWASI_P2_HE_IMMUTABLE = 2 };

/* -1 if (name, value) may be a field, else the header-error */
FUSEDWASI_P2_FN int fusedwasi_p2_http_check_entry(const uint8_t *name, size_t nlen, const uint8_t *value, size_t vlen) {
    static const char *const forbidden[] = {"connection", "keep-alive", "proxy-connection", "transfer-encoding", "upgrade", "host", "http2-settings", "te"};
    if (!fusedwasi_p2_http_is_token(name, nlen)) return FUSEDWASI_P2_HE_INVALID_SYNTAX;
    for (size_t i = 0; i < vlen; i++)
        if (value[i] == 0 || value[i] == 10 || value[i] == 13 || (value[i] < 32 && value[i] != 9) || value[i] == 127) return FUSEDWASI_P2_HE_INVALID_SYNTAX;
    for (size_t k = 0; k < sizeof forbidden / sizeof *forbidden; k++) {
        size_t n = strlen(forbidden[k]);
        if (n != nlen) continue;
        size_t i = 0;
        while (i < n && fusedwasi_p2_http_lower(name[i]) == forbidden[k][i]) i++;
        if (i == n) return FUSEDWASI_P2_HE_FORBIDDEN;
    }
    return FUSEDWASI_P2_HE_NONE;
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_destroy_fields(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    fusedwasi_p2_http_fields_unref((fusedwasi_p2_http_fields *)obj);
}

/* a new handle of fields; the table has the reference the caller had */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2_http_fields_register(fusedwasi_p2_state *state, fusedwasi_p2_http_fields *f) {
    if (!f) return 0; /* no memory for the fields */
    return fusedwasi_p2_table_add(state, FUSEDWASI_P2_KIND_FIELDS, f, fusedwasi_p2_http_destroy_fields); /* 0 when the table is full: the caller still has its reference */
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_http_fields_handle(fusedwasi_p2_guest *g, fusedwasi_p2_http_fields *f) {
    if (!f) g->trap(g, "out of memory");
    return fusedwasi_p2_handle_or_trap(g, fusedwasi_p2_http_fields_register(g->state, f));
}

FUSEDWASI_P2_FN fusedwasi_p2_http_fields *fusedwasi_p2_http_fields_get_obj(fusedwasi_p2_guest *g, uint32_t handle) {
    return (fusedwasi_p2_http_fields *)fusedwasi_p2_table_get(g, handle, FUSEDWASI_P2_KIND_FIELDS)->obj;
}

/* an own `fields` given to a request, a response or a body: the handle is spent, the fields are not to be changed any more; the caller owns the reference */
FUSEDWASI_P2_FN fusedwasi_p2_http_fields *fusedwasi_p2_http_fields_take(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_http_fields_get_obj(g, handle);
    fusedwasi_p2_http_fields *f = (fusedwasi_p2_http_fields *)fusedwasi_p2_table_take(g, handle, FUSEDWASI_P2_KIND_FIELDS);
    f->immutable = 1;
    return f;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_fields_new(fusedwasi_p2_guest *g) { return fusedwasi_p2_http_fields_handle(g, fusedwasi_p2_http_fields_new(0)); }

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_fields_from_list(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr, uint32_t len, uint32_t retptr) {
    for (uint32_t i = 0; i < len; i++) {
        uint32_t at = ptr + 16 * i;
        uint32_t np = fusedwasi_p2_load_u32(g, comp, at), nl = fusedwasi_p2_load_u32(g, comp, at + 4);
        uint32_t vp = fusedwasi_p2_load_u32(g, comp, at + 8), vl = fusedwasi_p2_load_u32(g, comp, at + 12);
        int err = fusedwasi_p2_http_check_entry(fusedwasi_p2_mem(g, comp, np, nl), nl, fusedwasi_p2_mem(g, comp, vp, vl), vl);
        if (err != FUSEDWASI_P2_HE_NONE) {
            fusedwasi_p2_store_u8(g, comp, retptr, 1);
            fusedwasi_p2_store_u8(g, comp, retptr + 4, (uint8_t)err);
            return;
        }
    }
    fusedwasi_p2_http_fields *f = fusedwasi_p2_http_fields_new(0);
    for (uint32_t i = 0; f && i < len; i++) {
        uint32_t at = ptr + 16 * i;
        uint32_t np = fusedwasi_p2_load_u32(g, comp, at), nl = fusedwasi_p2_load_u32(g, comp, at + 4);
        uint32_t vp = fusedwasi_p2_load_u32(g, comp, at + 8), vl = fusedwasi_p2_load_u32(g, comp, at + 12);
        if (fusedwasi_p2_http_fields_add(f, fusedwasi_p2_mem(g, comp, np, nl), nl, fusedwasi_p2_mem(g, comp, vp, vl), vl) != 0) {
            fusedwasi_p2_http_fields_unref(f);
            f = NULL;
        }
    }
    fusedwasi_p2_store_u8(g, comp, retptr, 0);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, fusedwasi_p2_http_fields_handle(g, f));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_fields_get(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t name_ptr, uint32_t name_len, uint32_t retptr) {
    fusedwasi_p2_http_fields *f = fusedwasi_p2_http_fields_get_obj(g, self);
    const uint8_t *name = fusedwasi_p2_mem(g, comp, name_ptr, name_len);
    uint32_t n = 0;
    for (uint32_t i = 0; i < f->count; i++) n += fusedwasi_p2_http_name_is(&f->e[i], name, name_len);
    uint32_t ptr = fusedwasi_p2_alloc(g, comp, 4, 8 * n), k = 0;
    for (uint32_t i = 0; i < f->count; i++)
        if (fusedwasi_p2_http_name_is(&f->e[i], fusedwasi_p2_mem(g, comp, name_ptr, name_len), name_len))
            fusedwasi_p2_http_store_string(g, comp, ptr + 8 * k++, f->e[i].value, f->e[i].len);
    fusedwasi_p2_store_u32(g, comp, retptr, ptr);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, n);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_fields_has(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t name_ptr, uint32_t name_len) {
    fusedwasi_p2_http_fields *f = fusedwasi_p2_http_fields_get_obj(g, self);
    const uint8_t *name = fusedwasi_p2_mem(g, comp, name_ptr, name_len);
    for (uint32_t i = 0; i < f->count; i++)
        if (fusedwasi_p2_http_name_is(&f->e[i], name, name_len)) return 1;
    return 0;
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_header_result(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, int err) {
    fusedwasi_p2_store_u8(g, comp, retptr, err == FUSEDWASI_P2_HE_NONE ? 0 : 1);
    if (err != FUSEDWASI_P2_HE_NONE) fusedwasi_p2_store_u8(g, comp, retptr + 1, (uint8_t)err);
}

typedef struct {
    const uint8_t *p;
    uint32_t n;
} fusedwasi_p2_http_span;

/* set (replace), append and delete: the values are spans of the memory of the guest */
FUSEDWASI_P2_FN int fusedwasi_p2_http_change(fusedwasi_p2_http_fields *f, const fusedwasi_p2_http_span name, const fusedwasi_p2_http_span *values, uint32_t count, int replace) {
    if (f->immutable) return FUSEDWASI_P2_HE_IMMUTABLE;
    if (count == 0) {
        int err = fusedwasi_p2_http_check_entry(name.p, name.n, name.p, 0);
        if (err != FUSEDWASI_P2_HE_NONE) return err;
    }
    for (uint32_t i = 0; i < count; i++) {
        int err = fusedwasi_p2_http_check_entry(name.p, name.n, values[i].p, values[i].n);
        if (err != FUSEDWASI_P2_HE_NONE) return err;
    }
    if (replace) fusedwasi_p2_http_fields_remove(f, name.p, name.n);
    for (uint32_t i = 0; i < count; i++) fusedwasi_p2_http_fields_add(f, name.p, name.n, values[i].p, values[i].n);
    return FUSEDWASI_P2_HE_NONE;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_fields_set(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t name_ptr, uint32_t name_len, uint32_t value_ptr,
                                           uint32_t value_len, uint32_t retptr) {
    fusedwasi_p2_http_fields *f = fusedwasi_p2_http_fields_get_obj(g, self);
    fusedwasi_p2_http_span name = {fusedwasi_p2_mem(g, comp, name_ptr, name_len), name_len};
    fusedwasi_p2_http_span *values = (fusedwasi_p2_http_span *)calloc(value_len ? value_len : 1, sizeof *values);
    if (!values) g->trap(g, "out of memory");
    for (uint32_t i = 0; i < value_len; i++) {
        uint32_t p = fusedwasi_p2_load_u32(g, comp, value_ptr + 8 * i), n = fusedwasi_p2_load_u32(g, comp, value_ptr + 8 * i + 4);
        values[i].p = fusedwasi_p2_mem(g, comp, p, n);
        values[i].n = n;
    }
    int err = fusedwasi_p2_http_change(f, name, values, value_len, 1);
    free(values);
    fusedwasi_p2_http_header_result(g, comp, retptr, err);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_fields_delete(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t name_ptr, uint32_t name_len, uint32_t retptr) {
    fusedwasi_p2_http_fields *f = fusedwasi_p2_http_fields_get_obj(g, self);
    fusedwasi_p2_http_span name = {fusedwasi_p2_mem(g, comp, name_ptr, name_len), name_len};
    fusedwasi_p2_http_header_result(g, comp, retptr, fusedwasi_p2_http_change(f, name, NULL, 0, 1));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_fields_append(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t name_ptr, uint32_t name_len, uint32_t value_ptr,
                                              uint32_t value_len, uint32_t retptr) {
    fusedwasi_p2_http_fields *f = fusedwasi_p2_http_fields_get_obj(g, self);
    fusedwasi_p2_http_span name = {fusedwasi_p2_mem(g, comp, name_ptr, name_len), name_len};
    fusedwasi_p2_http_span value = {fusedwasi_p2_mem(g, comp, value_ptr, value_len), value_len};
    fusedwasi_p2_http_header_result(g, comp, retptr, fusedwasi_p2_http_change(f, name, &value, 1, 0));
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_store_pairs(fusedwasi_p2_guest *g, uint32_t comp, const fusedwasi_p2_http_fields *f, uint32_t retptr) {
    uint32_t ptr = fusedwasi_p2_alloc(g, comp, 4, 16 * f->count);
    for (uint32_t i = 0; i < f->count; i++) {
        fusedwasi_p2_http_store_string(g, comp, ptr + 16 * i, f->e[i].name, (uint32_t)strlen(f->e[i].name));
        fusedwasi_p2_http_store_string(g, comp, ptr + 16 * i + 8, f->e[i].value, f->e[i].len);
    }
    fusedwasi_p2_store_u32(g, comp, retptr, ptr);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, f->count);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_fields_entries(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_store_pairs(g, comp, fusedwasi_p2_http_fields_get_obj(g, self), retptr);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_fields_clone(fusedwasi_p2_guest *g, uint32_t self) {
    fusedwasi_p2_http_fields *f = fusedwasi_p2_http_fields_get_obj(g, self), *c = fusedwasi_p2_http_fields_new(0);
    for (uint32_t i = 0; c && i < f->count; i++) fusedwasi_p2_http_fields_add(c, f->e[i].name, strlen(f->e[i].name), f->e[i].value, f->e[i].len);
    return fusedwasi_p2_http_fields_handle(g, c);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_fields_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_http_fields_get_obj(g, handle);
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_FIELDS);
}

/* blobs: bytes that a stream, an answer and a body share */

typedef struct fusedwasi_p2_http_blob {
    int refs;
    uint8_t *data;
    uint64_t len;
} fusedwasi_p2_http_blob;

FUSEDWASI_P2_FN fusedwasi_p2_http_blob *fusedwasi_p2_http_blob_new(const void *data, uint64_t len) {
    fusedwasi_p2_http_blob *b = (fusedwasi_p2_http_blob *)calloc(1, sizeof *b);
    if (!b) return NULL;
    b->data = (uint8_t *)malloc(len ? (size_t)len : 1);
    if (!b->data) {
        free(b);
        return NULL;
    }
    if (len) memcpy(b->data, data, (size_t)len);
    b->refs = 1;
    b->len = len;
    return b;
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_blob_unref(fusedwasi_p2_http_blob *b) {
    if (!b || --b->refs > 0) return;
    free(b->data);
    free(b);
}

/* method, scheme, authority, path */

static const char *const fusedwasi_p2_http_std_methods[9] = {"GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH"};

FUSEDWASI_P2_FN char *fusedwasi_p2_http_dup(const void *p, size_t n) {
    char *d = (char *)malloc(n + 1);
    if (!d) return NULL;
    if (n) memcpy(d, p, n);
    d[n] = 0;
    return d;
}

/* the method of the guest as text (malloc'd) */
FUSEDWASI_P2_FN char *fusedwasi_p2_http_lift_method(fusedwasi_p2_guest *g, uint32_t comp, uint32_t tag, uint32_t ptr, uint32_t n) {
    if (tag < 9) return fusedwasi_p2_http_dup(fusedwasi_p2_http_std_methods[tag], strlen(fusedwasi_p2_http_std_methods[tag]));
    if (tag > 9) g->trap(g, "bad method");
    return fusedwasi_p2_http_dup(fusedwasi_p2_mem(g, comp, ptr, n), n);
}

FUSEDWASI_P2_FN char *fusedwasi_p2_http_lift_scheme(fusedwasi_p2_guest *g, uint32_t comp, uint32_t tag, uint32_t ptr, uint32_t n) {
    if (tag == 0) return fusedwasi_p2_http_dup("http", 4);
    if (tag == 1) return fusedwasi_p2_http_dup("https", 5);
    if (tag > 2) g->trap(g, "bad scheme");
    return fusedwasi_p2_http_dup(fusedwasi_p2_mem(g, comp, ptr, n), n);
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_store_method(fusedwasi_p2_guest *g, uint32_t comp, uint32_t at, const char *method) {
    for (uint8_t i = 0; i < 9; i++)
        if (strcmp(method, fusedwasi_p2_http_std_methods[i]) == 0) {
            fusedwasi_p2_store_u8(g, comp, at, i);
            return;
        }
    fusedwasi_p2_store_u8(g, comp, at, 9);
    fusedwasi_p2_http_store_string(g, comp, at + 4, method, (uint32_t)strlen(method));
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_store_scheme(fusedwasi_p2_guest *g, uint32_t comp, uint32_t at, const char *scheme) {
    if (strcmp(scheme, "http") == 0) {
        fusedwasi_p2_store_u8(g, comp, at, 0);
    } else if (strcmp(scheme, "https") == 0) {
        fusedwasi_p2_store_u8(g, comp, at, 1);
    } else {
        fusedwasi_p2_store_u8(g, comp, at, 2);
        fusedwasi_p2_http_store_string(g, comp, at + 4, scheme, (uint32_t)strlen(scheme));
    }
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_store_opt_scheme(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, const char *scheme) {
    fusedwasi_p2_store_u8(g, comp, retptr, scheme ? 1 : 0);
    if (scheme) fusedwasi_p2_http_store_scheme(g, comp, retptr + 4, scheme);
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_valid_path(const uint8_t *p, size_t n) {
    if (n == 0 || (p[0] != '/' && !(n == 1 && p[0] == '*'))) return 0;
    for (size_t i = 0; i < n; i++)
        if (p[i] <= 32 || p[i] >= 127) return 0;
    return 1;
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_valid_scheme(const uint8_t *p, size_t n) {
    if (n == 0 || !((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'))) return 0;
    for (size_t i = 1; i < n; i++)
        if (!((p[i] >= 'A' && p[i] <= 'Z') || (p[i] >= 'a' && p[i] <= 'z') || (p[i] >= '0' && p[i] <= '9') || p[i] == '+' || p[i] == '.' || p[i] == '-')) return 0;
    return 1;
}

/* (host, port) of `host`, `host:port` or `[v6]:port`. Returns 1 and the host (malloc'd), or 0 if it is not an authority. */
FUSEDWASI_P2_FN int fusedwasi_p2_http_split_authority(const char *a, size_t n, int default_port, char **host_out, int *port_out) {
    if (n == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)a[i];
        if (c < 32 || c > 126 || strchr(" /@?#\\", c)) return 0;
    }
    size_t host_start = 0, host_len;
    const char *port_text;
    size_t port_len;
    if (a[0] == '[') {
        const char *end = (const char *)memchr(a, ']', n);
        if (!end) return 0;
        size_t close = (size_t)(end - a);
        host_start = 1;
        host_len = close - 1;
        size_t rest = n - close - 1;
        if (rest && a[close + 1] != ':') return 0;
        port_text = rest ? a + close + 2 : a + n;
        port_len = rest ? rest - 1 : 0;
    } else {
        const char *colon = (const char *)memchr(a, ':', n);
        host_len = colon ? (size_t)(colon - a) : n;
        port_text = colon ? colon + 1 : a + n;
        port_len = colon ? n - host_len - 1 : 0;
        if (memchr(port_text, ':', port_len)) return 0;
    }
    if (host_len == 0) return 0;
    if (port_len == 0 && (a[n - 1] == ':')) return 0; /* "host:" and "[v6]:" */
    int port = default_port;
    if (port_len) {
        long v = 0;
        for (size_t i = 0; i < port_len; i++) {
            if (port_text[i] < '0' || port_text[i] > '9') return 0;
            if (v < 1000000) v = v * 10 + (port_text[i] - '0');
        }
        if (v <= 0 || v >= 65536) return 0;
        port = (int)v;
    }
    if (host_out) {
        *host_out = fusedwasi_p2_http_dup(a + host_start, host_len);
        if (!*host_out) return 0;
    }
    if (port_out) *port_out = port;
    return 1;
}

/* bodies */

typedef struct fusedwasi_p2_http_obody {
    int refs;
    uint8_t *buf;
    uint64_t len, capacity;
    int has_expected;
    uint64_t expected; /* the content-length the guest declared */
    int response;      /* 1: the body of a response, 0: of a request */
    fusedwasi_p2_http_fields *trailers;
    uint32_t stream_handle;
    int stream_taken, finished, abandoned;
} fusedwasi_p2_http_obody;

FUSEDWASI_P2_FN void fusedwasi_p2_http_obody_unref(fusedwasi_p2_http_obody *b) {
    if (!b || --b->refs > 0) return;
    free(b->buf);
    fusedwasi_p2_http_fields_unref(b->trailers);
    free(b);
}

typedef struct {
    fusedwasi_p2_stream base;
    fusedwasi_p2_http_obody *body;
} fusedwasi_p2_http_ostream;

FUSEDWASI_P2_FN uint64_t fusedwasi_p2_http_ostream_check_write(fusedwasi_p2_state *state, fusedwasi_p2_stream *st, int *status) {
    (void)state;
    *status = FUSEDWASI_P2_STREAM_OK;
    return FUSEDWASI_P2_HTTP_MAX_BODY - ((fusedwasi_p2_http_ostream *)st)->body->len;
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_ostream_write(fusedwasi_p2_state *state, fusedwasi_p2_stream *st, const uint8_t *data, uint64_t len) {
    (void)state;
    fusedwasi_p2_http_obody *b = ((fusedwasi_p2_http_ostream *)st)->body;
    if (b->len + len > b->capacity) {
        uint64_t cap = b->capacity ? b->capacity : 64;
        while (cap < b->len + len) cap *= 2;
        uint8_t *grown = (uint8_t *)realloc(b->buf, (size_t)cap);
        if (!grown) {
            st->failure = "out of memory";
            st->failure_errno = ENOMEM;
            return FUSEDWASI_P2_STREAM_FAILED;
        }
        b->buf = grown;
        b->capacity = cap;
    }
    if (len) memcpy(b->buf + b->len, data, (size_t)len);
    b->len += len;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_ostream_flush(fusedwasi_p2_state *state, fusedwasi_p2_stream *st) {
    (void)state;
    (void)st;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_ostream_ready(fusedwasi_p2_state *state, fusedwasi_p2_stream *st) {
    int status;
    return fusedwasi_p2_http_ostream_check_write(state, st, &status) > 0;
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_ostream_destroy(fusedwasi_p2_state *state, fusedwasi_p2_stream *st) {
    (void)state;
    fusedwasi_p2_http_obody_unref(((fusedwasi_p2_http_ostream *)st)->body);
    free(st);
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_destroy_obody(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    fusedwasi_p2_http_obody *b = (fusedwasi_p2_http_obody *)obj;
    if (!b->finished) b->abandoned = 1;
    fusedwasi_p2_http_obody_unref(b);
}

FUSEDWASI_P2_FN fusedwasi_p2_http_obody *fusedwasi_p2_http_obody_get(fusedwasi_p2_guest *g, uint32_t handle) {
    return (fusedwasi_p2_http_obody *)fusedwasi_p2_table_get(g, handle, FUSEDWASI_P2_KIND_OUTGOING_BODY)->obj;
}

/* the content-length of the fields, if it is a number (the first that is) */
FUSEDWASI_P2_FN int fusedwasi_p2_http_declared_length(const fusedwasi_p2_http_fields *f, uint64_t *out) {
    for (uint32_t i = 0; i < f->count; i++) {
        if (strcmp(f->e[i].name, "content-length") != 0 || f->e[i].len == 0) continue;
        uint64_t v = 0;
        uint32_t k = 0;
        for (; k < f->e[i].len && f->e[i].value[k] >= '0' && f->e[i].value[k] <= '9'; k++) {
            uint64_t d = (uint64_t)(f->e[i].value[k] - '0');
            v = v > (UINT64_MAX - d) / 10 ? UINT64_MAX : v * 10 + d;
        }
        if (k == f->e[i].len) {
            *out = v;
            return 1;
        }
    }
    return 0;
}

/* `body()` of a request or a response: makes the body once, then err. Returns the new body (the caller keeps its reference in `*slot`), or NULL. */
FUSEDWASI_P2_FN void fusedwasi_p2_http_new_body(fusedwasi_p2_guest *g, uint32_t comp, const fusedwasi_p2_http_fields *headers, int response, fusedwasi_p2_http_obody **slot, uint32_t retptr) {
    if (*slot) {
        fusedwasi_p2_store_u8(g, comp, retptr, 1);
        return;
    }
    fusedwasi_p2_http_obody *b = (fusedwasi_p2_http_obody *)calloc(1, sizeof *b);
    if (!b) g->trap(g, "out of memory");
    b->refs = 2; /* the owner and the handle */
    b->response = response;
    b->has_expected = fusedwasi_p2_http_declared_length(headers, &b->expected);
    *slot = b;
    fusedwasi_p2_store_u8(g, comp, retptr, 0);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_OUTGOING_BODY, b, fusedwasi_p2_http_destroy_obody));
}

/* outgoing-request */

typedef struct fusedwasi_p2_http_orequest {
    fusedwasi_p2_http_fields *headers;
    char *method, *path, *scheme, *authority;
    fusedwasi_p2_http_obody *body;
} fusedwasi_p2_http_orequest;

FUSEDWASI_P2_FN void fusedwasi_p2_http_orequest_free(fusedwasi_p2_http_orequest *r) {
    if (!r) return;
    fusedwasi_p2_http_fields_unref(r->headers);
    free(r->method), free(r->path), free(r->scheme), free(r->authority);
    fusedwasi_p2_http_obody_unref(r->body);
    free(r);
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_destroy_orequest(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    fusedwasi_p2_http_orequest_free((fusedwasi_p2_http_orequest *)obj);
}

FUSEDWASI_P2_FN fusedwasi_p2_http_orequest *fusedwasi_p2_http_orequest_get(fusedwasi_p2_guest *g, uint32_t handle) {
    return (fusedwasi_p2_http_orequest *)fusedwasi_p2_table_get(g, handle, FUSEDWASI_P2_KIND_OUTGOING_REQUEST)->obj;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_outgoing_request_new(fusedwasi_p2_guest *g, uint32_t headers) {
    fusedwasi_p2_http_orequest *r = (fusedwasi_p2_http_orequest *)calloc(1, sizeof *r);
    if (!r) g->trap(g, "out of memory");
    r->headers = fusedwasi_p2_http_fields_take(g, headers);
    r->method = fusedwasi_p2_http_dup("GET", 3);
    return fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_OUTGOING_REQUEST, r, fusedwasi_p2_http_destroy_orequest);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_outgoing_request_body(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_orequest *r = fusedwasi_p2_http_orequest_get(g, self);
    fusedwasi_p2_http_new_body(g, comp, r->headers, 0, &r->body, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_outgoing_request_method(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_store_method(g, comp, retptr, fusedwasi_p2_http_orequest_get(g, self)->method);
}

/* replaces a text of a request: returns 0, or 1 for "no" (`value` is freed then) */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2_http_replace(char **slot, char *value) {
    if (!value) return 1;
    free(*slot);
    *slot = value;
    return 0;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_outgoing_request_set_method(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t tag, uint32_t ptr, uint32_t len) {
    fusedwasi_p2_http_orequest *r = fusedwasi_p2_http_orequest_get(g, self);
    char *method = fusedwasi_p2_http_lift_method(g, comp, tag, ptr, len);
    if (!method || !fusedwasi_p2_http_is_token((const uint8_t *)method, strlen(method)) || strlen(method) != (tag == 9 ? len : strlen(method))) {
        free(method);
        return 1;
    }
    return fusedwasi_p2_http_replace(&r->method, method);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_outgoing_request_path_with_query(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_store_opt_string(g, comp, retptr, fusedwasi_p2_http_orequest_get(g, self)->path);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_outgoing_request_set_path_with_query(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t tag, uint32_t ptr, uint32_t len) {
    fusedwasi_p2_http_orequest *r = fusedwasi_p2_http_orequest_get(g, self);
    if (!tag) {
        free(r->path);
        r->path = NULL;
        return 0;
    }
    const uint8_t *p = fusedwasi_p2_mem(g, comp, ptr, len);
    if (!fusedwasi_p2_http_valid_path(p, len)) return 1;
    return fusedwasi_p2_http_replace(&r->path, fusedwasi_p2_http_dup(p, len));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_outgoing_request_scheme(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_store_opt_scheme(g, comp, retptr, fusedwasi_p2_http_orequest_get(g, self)->scheme);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_outgoing_request_set_scheme(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t tag, uint32_t scheme_tag, uint32_t ptr,
                                                                uint32_t len) {
    fusedwasi_p2_http_orequest *r = fusedwasi_p2_http_orequest_get(g, self);
    if (!tag) {
        free(r->scheme);
        r->scheme = NULL;
        return 0;
    }
    char *scheme = fusedwasi_p2_http_lift_scheme(g, comp, scheme_tag, ptr, len);
    if (!scheme || (scheme_tag == 2 && !fusedwasi_p2_http_valid_scheme((const uint8_t *)scheme, len))) {
        free(scheme);
        return 1;
    }
    return fusedwasi_p2_http_replace(&r->scheme, scheme);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_outgoing_request_authority(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_store_opt_string(g, comp, retptr, fusedwasi_p2_http_orequest_get(g, self)->authority);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_outgoing_request_set_authority(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t tag, uint32_t ptr, uint32_t len) {
    fusedwasi_p2_http_orequest *r = fusedwasi_p2_http_orequest_get(g, self);
    if (!tag) {
        free(r->authority);
        r->authority = NULL;
        return 0;
    }
    const char *a = (const char *)fusedwasi_p2_mem(g, comp, ptr, len);
    if (!fusedwasi_p2_http_split_authority(a, len, 80, NULL, NULL)) return 1;
    return fusedwasi_p2_http_replace(&r->authority, fusedwasi_p2_http_dup(a, len));
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_outgoing_request_headers(fusedwasi_p2_guest *g, uint32_t self) {
    fusedwasi_p2_http_fields *f = fusedwasi_p2_http_orequest_get(g, self)->headers;
    fusedwasi_p2_http_fields_ref(f);
    return fusedwasi_p2_http_fields_handle(g, f);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_outgoing_request_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_http_orequest_get(g, handle);
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_OUTGOING_REQUEST);
}

/* request-options */

typedef struct {
    int has[3]; /* connect, first byte, between bytes */
    uint64_t ns[3];
} fusedwasi_p2_http_options;

FUSEDWASI_P2_FN fusedwasi_p2_http_options *fusedwasi_p2_http_options_get(fusedwasi_p2_guest *g, uint32_t handle) {
    return (fusedwasi_p2_http_options *)fusedwasi_p2_table_get(g, handle, FUSEDWASI_P2_KIND_REQUEST_OPTIONS)->obj;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_request_options_new(fusedwasi_p2_guest *g) {
    void *o = calloc(1, sizeof(fusedwasi_p2_http_options));
    if (!o) g->trap(g, "out of memory");
    return fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_REQUEST_OPTIONS, o, fusedwasi_p2_free_obj);
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_get_timeout(fusedwasi_p2_guest *g, uint32_t comp, const fusedwasi_p2_http_options *o, int which, uint32_t retptr) {
    fusedwasi_p2_store_u8(g, comp, retptr, o->has[which] ? 1 : 0);
    if (o->has[which]) fusedwasi_p2_store_u64(g, comp, retptr + 8, o->ns[which]);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_http_set_timeout(fusedwasi_p2_http_options *o, int which, uint32_t tag, uint64_t value) {
    o->has[which] = tag ? 1 : 0;
    o->ns[which] = tag ? value : 0;
    return 0;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_request_options_connect_timeout(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_get_timeout(g, comp, fusedwasi_p2_http_options_get(g, self), 0, retptr);
}
FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_request_options_set_connect_timeout(fusedwasi_p2_guest *g, uint32_t self, uint32_t tag, uint64_t value) {
    return fusedwasi_p2_http_set_timeout(fusedwasi_p2_http_options_get(g, self), 0, tag, value);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_request_options_first_byte_timeout(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_get_timeout(g, comp, fusedwasi_p2_http_options_get(g, self), 1, retptr);
}
FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_request_options_set_first_byte_timeout(fusedwasi_p2_guest *g, uint32_t self, uint32_t tag, uint64_t value) {
    return fusedwasi_p2_http_set_timeout(fusedwasi_p2_http_options_get(g, self), 1, tag, value);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_request_options_between_bytes_timeout(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_get_timeout(g, comp, fusedwasi_p2_http_options_get(g, self), 2, retptr);
}
FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_request_options_set_between_bytes_timeout(fusedwasi_p2_guest *g, uint32_t self, uint32_t tag, uint64_t value) {
    return fusedwasi_p2_http_set_timeout(fusedwasi_p2_http_options_get(g, self), 2, tag, value);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_request_options_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_http_options_get(g, handle);
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_REQUEST_OPTIONS);
}

/* incoming bodies, and the messages that carry them */

typedef struct {
    fusedwasi_p2_memory_input base;
    fusedwasi_p2_http_blob *blob;
} fusedwasi_p2_http_istream;

FUSEDWASI_P2_FN void fusedwasi_p2_http_istream_destroy(fusedwasi_p2_state *state, fusedwasi_p2_stream *st) {
    (void)state;
    fusedwasi_p2_http_blob_unref(((fusedwasi_p2_http_istream *)st)->blob);
    free(st);
}

typedef struct {
    fusedwasi_p2_http_blob *blob;
    fusedwasi_p2_http_fields *trailers;
    uint32_t stream_handle;
    int stream_taken;
} fusedwasi_p2_http_ibody;

FUSEDWASI_P2_FN void fusedwasi_p2_http_ibody_free(fusedwasi_p2_http_ibody *b) {
    fusedwasi_p2_http_blob_unref(b->blob);
    fusedwasi_p2_http_fields_unref(b->trailers);
    free(b);
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_destroy_ibody(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    fusedwasi_p2_http_ibody_free((fusedwasi_p2_http_ibody *)obj);
}

FUSEDWASI_P2_FN fusedwasi_p2_http_ibody *fusedwasi_p2_http_ibody_get(fusedwasi_p2_guest *g, uint32_t handle) {
    return (fusedwasi_p2_http_ibody *)fusedwasi_p2_table_get(g, handle, FUSEDWASI_P2_KIND_INCOMING_BODY)->obj;
}

/* the body of a received message, to be consumed once */
typedef struct {
    fusedwasi_p2_http_blob *blob;
    fusedwasi_p2_http_fields *trailers;
    int consumed;
} fusedwasi_p2_http_payload;

FUSEDWASI_P2_FN void fusedwasi_p2_http_payload_free(fusedwasi_p2_http_payload *p) {
    fusedwasi_p2_http_blob_unref(p->blob);
    fusedwasi_p2_http_fields_unref(p->trailers);
}

/* `consume`: `result<incoming-body>` */
FUSEDWASI_P2_FN void fusedwasi_p2_http_consume(fusedwasi_p2_guest *g, uint32_t comp, fusedwasi_p2_http_payload *p, uint32_t retptr) {
    if (p->consumed) {
        fusedwasi_p2_store_u8(g, comp, retptr, 1);
        return;
    }
    p->consumed = 1;
    fusedwasi_p2_http_ibody *b = (fusedwasi_p2_http_ibody *)calloc(1, sizeof *b);
    if (!b) g->trap(g, "out of memory");
    b->blob = p->blob;
    b->trailers = p->trailers;
    p->blob = NULL;
    p->trailers = NULL;
    fusedwasi_p2_store_u8(g, comp, retptr, 0);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_INCOMING_BODY, b, fusedwasi_p2_http_destroy_ibody));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_incoming_body_stream(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_ibody *b = fusedwasi_p2_http_ibody_get(g, self);
    if (b->stream_taken) {
        fusedwasi_p2_store_u8(g, comp, retptr, 1);
        return;
    }
    b->stream_taken = 1;
    fusedwasi_p2_http_istream *st = (fusedwasi_p2_http_istream *)calloc(1, sizeof *st);
    if (!st) g->trap(g, "out of memory");
    st->base.base.read = fusedwasi_p2_memory_input_read;
    st->base.base.ready = fusedwasi_p2_always_ready;
    st->base.base.destroy = fusedwasi_p2_http_istream_destroy;
    st->base.data = b->blob ? b->blob->data : NULL;
    st->base.len = b->blob ? b->blob->len : 0;
    st->blob = b->blob;
    if (b->blob) b->blob->refs++;
    b->stream_handle = fusedwasi_p2_input_stream_new(g, &st->base.base);
    fusedwasi_p2_store_u8(g, comp, retptr, 0);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, b->stream_handle);
}

typedef struct {
    fusedwasi_p2_http_fields *trailers;
    int taken;
} fusedwasi_p2_http_future_trailers;

FUSEDWASI_P2_FN void fusedwasi_p2_http_destroy_future_trailers(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    fusedwasi_p2_http_future_trailers *f = (fusedwasi_p2_http_future_trailers *)obj;
    fusedwasi_p2_http_fields_unref(f->trailers);
    free(f);
}

FUSEDWASI_P2_FN fusedwasi_p2_http_future_trailers *fusedwasi_p2_http_future_trailers_get(fusedwasi_p2_guest *g, uint32_t handle) {
    return (fusedwasi_p2_http_future_trailers *)fusedwasi_p2_table_get(g, handle, FUSEDWASI_P2_KIND_FUTURE_TRAILERS)->obj;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_incoming_body_finish(fusedwasi_p2_guest *g, uint32_t this_) {
    fusedwasi_p2_http_ibody *b = fusedwasi_p2_http_ibody_get(g, this_);
    if (b->stream_handle && fusedwasi_p2_table_alive(g, b->stream_handle, FUSEDWASI_P2_KIND_INPUT_STREAM)) g->trap(g, "incoming-body.finish with its stream alive");
    fusedwasi_p2_http_future_trailers *f = (fusedwasi_p2_http_future_trailers *)calloc(1, sizeof *f);
    if (!f) g->trap(g, "out of memory");
    f->trailers = b->trailers;
    b->trailers = NULL;
    fusedwasi_p2_table_drop(g, this_, FUSEDWASI_P2_KIND_INCOMING_BODY);
    return fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_FUTURE_TRAILERS, f, fusedwasi_p2_http_destroy_future_trailers);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_incoming_body_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_http_ibody_get(g, handle);
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_INCOMING_BODY);
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_ready_always(fusedwasi_p2_state *state, fusedwasi_p2_pollable *p) {
    (void)state;
    (void)p;
    return 1;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_future_trailers_subscribe(fusedwasi_p2_guest *g, uint32_t self) {
    fusedwasi_p2_http_future_trailers_get(g, self);
    fusedwasi_p2_pollable *p = (fusedwasi_p2_pollable *)calloc(1, sizeof *p);
    if (!p) g->trap(g, "out of memory");
    p->ready = fusedwasi_p2_http_ready_always;
    return fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_POLLABLE, p, fusedwasi_p2_free_obj);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_future_trailers_get(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_future_trailers *f = fusedwasi_p2_http_future_trailers_get(g, self);
    fusedwasi_p2_store_u8(g, comp, retptr, 1);
    if (f->taken) {
        fusedwasi_p2_store_u8(g, comp, retptr + 8, 1);
        return;
    }
    f->taken = 1;
    fusedwasi_p2_store_u8(g, comp, retptr + 8, 0);
    fusedwasi_p2_store_u8(g, comp, retptr + 16, 0);
    if (!f->trailers) {
        fusedwasi_p2_store_u8(g, comp, retptr + 24, 0);
    } else {
        fusedwasi_p2_store_u8(g, comp, retptr + 24, 1);
        fusedwasi_p2_http_fields *t = f->trailers;
        fusedwasi_p2_http_fields_ref(t);
        fusedwasi_p2_store_u32(g, comp, retptr + 28, fusedwasi_p2_http_fields_handle(g, t));
    }
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_future_trailers_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_http_future_trailers_get(g, handle);
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_FUTURE_TRAILERS);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_outgoing_body_write(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_obody *b = fusedwasi_p2_http_obody_get(g, self);
    if (b->stream_taken) {
        fusedwasi_p2_store_u8(g, comp, retptr, 1);
        return;
    }
    fusedwasi_p2_http_ostream *st = (fusedwasi_p2_http_ostream *)calloc(1, sizeof *st);
    if (!st) g->trap(g, "out of memory");
    st->base.check_write = fusedwasi_p2_http_ostream_check_write;
    st->base.write = fusedwasi_p2_http_ostream_write;
    st->base.flush = fusedwasi_p2_http_ostream_flush;
    st->base.ready = fusedwasi_p2_http_ostream_ready;
    st->base.destroy = fusedwasi_p2_http_ostream_destroy;
    st->body = b;
    b->refs++;
    b->stream_taken = 1;
    b->stream_handle = fusedwasi_p2_output_stream_new(g, &st->base);
    fusedwasi_p2_store_u8(g, comp, retptr, 0);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, b->stream_handle);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_outgoing_body_finish(fusedwasi_p2_guest *g, uint32_t comp, uint32_t this_, uint32_t tag, uint32_t trailers, uint32_t retptr) {
    fusedwasi_p2_http_obody *b = fusedwasi_p2_http_obody_get(g, this_);
    if (b->stream_handle && fusedwasi_p2_table_alive(g, b->stream_handle, FUSEDWASI_P2_KIND_OUTPUT_STREAM)) g->trap(g, "outgoing-body.finish with its stream alive");
    if (tag) {
        fusedwasi_p2_http_fields_unref(b->trailers);
        b->trailers = fusedwasi_p2_http_fields_take(g, trailers);
    }
    int mismatch = b->has_expected && b->expected != b->len;
    if (mismatch) b->abandoned = 1;
    else b->finished = 1; /* before the handle is dropped: a drop of an unfinished body abandons it */
    b->refs++;            /* the handle may hold the last reference */
    fusedwasi_p2_table_drop(g, this_, FUSEDWASI_P2_KIND_OUTGOING_BODY);
    if (mismatch) {
        fusedwasi_p2_http_error e;
        fusedwasi_p2_http_error_init(&e, 0);
        fusedwasi_p2_http_fail_num(&e, b->response ? FUSEDWASI_P2_HTTP_RESPONSE_BODY_SIZE : FUSEDWASI_P2_HTTP_REQUEST_BODY_SIZE, b->len);
        fusedwasi_p2_store_u8(g, comp, retptr, 1);
        fusedwasi_p2_http_store_error(g, comp, retptr + 8, &e);
        fusedwasi_p2_http_error_free(&e);
    } else {
        fusedwasi_p2_store_u8(g, comp, retptr, 0);
    }
    fusedwasi_p2_http_obody_unref(b);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_outgoing_body_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_http_obody_get(g, handle);
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_OUTGOING_BODY);
}

/* the transport */

typedef struct fusedwasi_p2_http_request {
    const char *method, *scheme, *authority, *path;
    const fusedwasi_p2_http_fields *headers;
    const uint8_t *body;
    uint64_t body_len;
    const fusedwasi_p2_http_fields *trailers; /* NULL: none (a body with Content-Length) */
    int has_timeout[3];                  /* connect, first byte, between bytes */
    uint64_t timeout_ns[3];
} fusedwasi_p2_http_request;

/* what a transport gives back: the host owns the references afterwards */
typedef struct fusedwasi_p2_http_response {
    uint16_t status;
    fusedwasi_p2_http_fields *headers;
    fusedwasi_p2_http_blob *body;
    fusedwasi_p2_http_fields *trailers; /* NULL: none */
} fusedwasi_p2_http_response;


FUSEDWASI_P2_FN int fusedwasi_p2_http_ms(const fusedwasi_p2_http_request *req, int which) {
    uint64_t ns = req->has_timeout[which] ? req->timeout_ns[which] : FUSEDWASI_P2_HTTP_DEFAULT_TIMEOUT_NS;
    uint64_t ms = ns / 1000000 + (ns % 1000000 ? 1 : 0);
    if (ms == 0) ms = 1;
    return ms > (uint64_t)INT_MAX ? INT_MAX : (int)ms;
}

/* the text of an errno in a buffer of the caller: strerror() may return one buffer shared by every thread, so it is not used */
FUSEDWASI_P2_FN const char *fusedwasi_p2_http_strerror(int e, char *buf, size_t n) {
#if defined(_WIN32)
    if (strerror_s(buf, n, e) != 0) snprintf(buf, n, "error %d", e);
#elif defined(__GLIBC__) && defined(__USE_GNU)  /* the GNU strerror_r gives a string, which need not be in the buffer */
    const char *text = strerror_r(e, buf, n);
    if (text != buf) snprintf(buf, n, "%s", text);
#else  /* POSIX: 0 on success */
    if (strerror_r(e, buf, n) != 0) snprintf(buf, n, "error %d", e);
#endif
    return buf;
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_connect_error(fusedwasi_p2_http_error *err, int e) {
    switch (e) {
    case ECONNREFUSED: return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_CONNECTION_REFUSED);
    case ETIMEDOUT: return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_CONNECTION_TIMEOUT);
    case EHOSTUNREACH: case ENETUNREACH: return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_DESTINATION_UNAVAILABLE);
    case ECONNRESET: case ECONNABORTED: case EPIPE: return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_CONNECTION_TERMINATED);
    default: {
        char text[128];
        return fusedwasi_p2_http_fail_str(err, FUSEDWASI_P2_HTTP_INTERNAL_ERROR, 0, fusedwasi_p2_http_strerror(e, text, sizeof text));
    }
    }
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_dns_error(fusedwasi_p2_http_error *err, int rc) {
    const char *text;
#ifdef EAI_NODATA
    if (rc == EAI_NONAME || rc == EAI_NODATA) {
#else
    if (rc == EAI_NONAME) {
#endif
        text = "NXDOMAIN";
    } else if (rc == EAI_AGAIN) {
        text = "SERVFAIL";
    } else {
        text = gai_strerror(rc);
    }
    return fusedwasi_p2_http_fail_str(err, FUSEDWASI_P2_HTTP_DNS_ERROR, 0, text);
}

/* waits for the descriptor: 1 ready, 0 timeout, -1 error */
FUSEDWASI_P2_FN int fusedwasi_p2_http_wait_fd(int fd, short events, int ms) {
    struct pollfd p = {fd, events, 0};
    for (;;) {
        int r = FUSEDWASI_P2_POLL(&p, 1, ms);
        if (r < 0 && fusedwasi_p2_errno() == EINTR) continue;
        return r;
    }
}

/* connects to host:port; returns the descriptor, or -1 with `err` set */
FUSEDWASI_P2_FN int fusedwasi_p2_http_connect(const char *host, int port, int ms, fusedwasi_p2_http_error *err) {
    struct addrinfo hints, *list = NULL;
    char service[16];
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof service, "%d", port);
    fusedwasi_p2_net_start();
    int rc = getaddrinfo(host, service, &hints, &list);
    if (rc != 0) {
        fusedwasi_p2_http_dns_error(err, rc);
        return -1;
    }
    int have_error = 0;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        int fd = (int)socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) {
            fusedwasi_p2_http_connect_error(err, fusedwasi_p2_errno());
            have_error = 1;
            continue;
        }
        fusedwasi_p2_set_blocking(fd, 0);
        int failed = 0, code = 0;
        if (connect(fd, a->ai_addr, a->ai_addrlen) != 0) {
            if (fusedwasi_p2_errno() != EINPROGRESS && fusedwasi_p2_errno() != EWOULDBLOCK) {
                failed = 1;
                code = fusedwasi_p2_errno();
            } else {
                int w = fusedwasi_p2_http_wait_fd(fd, POLLOUT, ms);
                if (w == 0) {
                    failed = 2;
                } else if (w < 0) {
                    failed = 1;
                    code = fusedwasi_p2_errno();
                } else {
                    socklen_t n = sizeof code;
                    FUSEDWASI_P2_GETSOCKOPT(fd, SOL_SOCKET, SO_ERROR, &code, &n);
                    code = fusedwasi_p2_sockerr(code);
                    if (code) failed = 1;
                }
            }
        }
        if (!failed) {
            fusedwasi_p2_set_blocking(fd, 1);
            freeaddrinfo(list);
            return fd;
        }
        FUSEDWASI_P2_CLOSE(fd);
        if (failed == 2)
            fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_CONNECTION_TIMEOUT);
        else
            fusedwasi_p2_http_connect_error(err, code);
        have_error = 1;
    }
    freeaddrinfo(list);
    if (!have_error) fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_DESTINATION_NOT_FOUND);
    return -1;
}

typedef struct {
    int fd;
    uint8_t *buf;
    size_t len, cap, pos; /* the bytes from pos to len are not used yet */
    int first, first_ms, between_ms;
} fusedwasi_p2_http_reader;

/* reads more: 1 got data, 0 end of the stream, -1 error (err set) */
FUSEDWASI_P2_FN int fusedwasi_p2_http_more(fusedwasi_p2_http_reader *r, fusedwasi_p2_http_error *err) {
    if (r->pos > 0 && r->pos == r->len) r->pos = r->len = 0;
    if (r->cap - r->len < 65536) {
        size_t cap = r->cap ? r->cap * 2 : 131072;
        while (cap - r->len < 65536) cap *= 2;
        uint8_t *grown = (uint8_t *)realloc(r->buf, cap);
        if (!grown) return fusedwasi_p2_http_fail_str(err, FUSEDWASI_P2_HTTP_INTERNAL_ERROR, 0, "out of memory"), -1;
        r->buf = grown;
        r->cap = cap;
    }
    int w = fusedwasi_p2_http_wait_fd(r->fd, POLLIN, r->first ? r->first_ms : r->between_ms);
    if (w == 0) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_CONNECTION_READ_TIMEOUT), -1;
    if (w < 0) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_CONNECTION_TERMINATED), -1;
    fusedwasi_p2_ssize n;
    do n = FUSEDWASI_P2_RECV(r->fd, r->buf + r->len, 65536, 0);
    while (n < 0 && fusedwasi_p2_errno() == EINTR);
    if (n < 0) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_CONNECTION_TERMINATED), -1;
    r->first = 0;
    r->len += (size_t)n;
    return n > 0;
}

/* a line without its CRLF, in a buffer the caller frees. Returns 0, or 1 with err set. */
FUSEDWASI_P2_FN int fusedwasi_p2_http_line(fusedwasi_p2_http_reader *r, fusedwasi_p2_http_error *err, uint8_t **line, size_t *n) {
    size_t scanned = 0;
    for (;;) {
        for (size_t i = r->pos + scanned; i + 1 < r->len; i++) {
            if (r->buf[i] == '\r' && r->buf[i + 1] == '\n') {
                *line = (uint8_t *)fusedwasi_p2_http_dup(r->buf + r->pos, i - r->pos);
                if (!*line) return fusedwasi_p2_http_fail_str(err, FUSEDWASI_P2_HTTP_INTERNAL_ERROR, 0, "out of memory");
                *n = i - r->pos;
                r->pos = i + 2;
                return 0;
            }
        }
        scanned = r->len - r->pos > 0 ? r->len - r->pos - 1 : 0;
        if (r->len - r->pos > FUSEDWASI_P2_HTTP_MAX_HEAD) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_PROTOCOL_ERROR);
        int m = fusedwasi_p2_http_more(r, err);
        if (m < 0) return 1;
        if (m == 0) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_RESPONSE_INCOMPLETE);
    }
}

/* exactly n bytes into a new buffer. Returns 0, or 1 with err set. */
FUSEDWASI_P2_FN int fusedwasi_p2_http_exact(fusedwasi_p2_http_reader *r, fusedwasi_p2_http_error *err, size_t n, uint8_t **out) {
    while (r->len - r->pos < n) {
        int m = fusedwasi_p2_http_more(r, err);
        if (m < 0) return 1;
        if (m == 0) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_RESPONSE_INCOMPLETE);
    }
    *out = (uint8_t *)fusedwasi_p2_http_dup(r->buf + r->pos, n);
    if (!*out) return fusedwasi_p2_http_fail_str(err, FUSEDWASI_P2_HTTP_INTERNAL_ERROR, 0, "out of memory");
    r->pos += n;
    return 0;
}

/* the fields in `lines` (a head or a trailer): name, colon, value; the value is trimmed of spaces and tabs */
FUSEDWASI_P2_FN int fusedwasi_p2_http_parse_field(fusedwasi_p2_http_fields *f, const uint8_t *line, size_t n, fusedwasi_p2_http_error *err) {
    const uint8_t *colon = n ? (const uint8_t *)memchr(line, ':', n) : NULL;
    if (!colon || line[0] == ' ' || line[0] == '\t' || !fusedwasi_p2_http_is_token(line, (size_t)(colon - line))) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_PROTOCOL_ERROR);
    const uint8_t *v = colon + 1, *end = line + n;
    while (v < end && (*v == ' ' || *v == '\t')) v++;
    while (end > v && (end[-1] == ' ' || end[-1] == '\t')) end--;
    if (fusedwasi_p2_http_fields_add(f, line, (size_t)(colon - line), v, (size_t)(end - v)) != 0) return fusedwasi_p2_http_fail_str(err, FUSEDWASI_P2_HTTP_INTERNAL_ERROR, 0, "out of memory");
    return 0;
}

/* the status line and the fields; an informational answer is skipped. Returns 0, or 1 with err set. */
FUSEDWASI_P2_FN int fusedwasi_p2_http_read_head(fusedwasi_p2_http_reader *r, fusedwasi_p2_http_error *err, uint16_t *status, fusedwasi_p2_http_fields *headers) {
    for (;;) {
        uint8_t *status_line = NULL;
        size_t status_len = 0, head_len = 0;
        fusedwasi_p2_http_fields *fields = fusedwasi_p2_http_fields_new(0);
        if (!fields) return fusedwasi_p2_http_fail_str(err, FUSEDWASI_P2_HTTP_INTERNAL_ERROR, 0, "out of memory");
        for (;;) {
            uint8_t *line = NULL;
            size_t n = 0;
            if (fusedwasi_p2_http_line(r, err, &line, &n)) {
                free(status_line);
                fusedwasi_p2_http_fields_unref(fields);
                return 1;
            }
            if (n == 0 && !status_line) {
                free(line);
                continue; /* a blank line before the status line */
            }
            if (n == 0) {
                free(line);
                break;
            }
            head_len += n + 1;
            if (head_len > FUSEDWASI_P2_HTTP_MAX_HEAD) {
                free(line), free(status_line);
                fusedwasi_p2_http_fields_unref(fields);
                return fusedwasi_p2_http_fail_num(err, FUSEDWASI_P2_HTTP_RESPONSE_HEADER_SECTION_SIZE, FUSEDWASI_P2_HTTP_MAX_HEAD);
            }
            if (!status_line) {
                status_line = line, status_len = n;
                continue;
            }
            int bad = fusedwasi_p2_http_parse_field(fields, line, n, err);
            free(line);
            if (bad) {
                free(status_line);
                fusedwasi_p2_http_fields_unref(fields);
                return 1;
            }
        }
        int ok = status_len >= 12 && memcmp(status_line, "HTTP/1.", 7) == 0 && (status_line[7] == '0' || status_line[7] == '1') && status_line[8] == ' ' &&
                 status_line[9] >= '0' && status_line[9] <= '9' && status_line[10] >= '0' && status_line[10] <= '9' && status_line[11] >= '0' &&
                 status_line[11] <= '9' && (status_len == 12 || status_line[12] == ' ');
        if (!ok) {
            free(status_line);
            fusedwasi_p2_http_fields_unref(fields);
            return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_PROTOCOL_ERROR);
        }
        int code = (status_line[9] - '0') * 100 + (status_line[10] - '0') * 10 + (status_line[11] - '0');
        free(status_line);
        if (code == 101) {
            fusedwasi_p2_http_fields_unref(fields);
            return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_UPGRADE_FAILED);
        }
        if (code < 200) {
            fusedwasi_p2_http_fields_unref(fields);
            continue; /* informational */
        }
        *status = (uint16_t)code;
        for (uint32_t i = 0; i < fields->count; i++) {
            fusedwasi_p2_http_fields_add(headers, fields->e[i].name, strlen(fields->e[i].name), fields->e[i].value, fields->e[i].len);
            if (headers->count != i + 1) break;
        }
        fusedwasi_p2_http_fields_unref(fields);
        return 0;
    }
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_is_hex(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

/* appends to a growing buffer */
FUSEDWASI_P2_FN int fusedwasi_p2_http_append(uint8_t **buf, size_t *len, size_t *cap, const uint8_t *data, size_t n) {
    if (*len + n > *cap) {
        size_t c = *cap ? *cap : 4096;
        while (c < *len + n) c *= 2;
        uint8_t *grown = (uint8_t *)realloc(*buf, c);
        if (!grown) return -1;
        *buf = grown;
        *cap = c;
    }
    if (n) memcpy(*buf + *len, data, n);
    *len += n;
    return 0;
}

#define FUSEDWASI_P2_HTTP_OOM(err) fusedwasi_p2_http_fail_str((err), FUSEDWASI_P2_HTTP_INTERNAL_ERROR, 0, "out of memory")

/* a body in chunks, then the trailers. Returns 0, or 1 with err set; `*trailers` is NULL if there are none. */
FUSEDWASI_P2_FN int fusedwasi_p2_http_read_chunked(fusedwasi_p2_http_reader *r, fusedwasi_p2_http_error *err, uint8_t **body, size_t *len, fusedwasi_p2_http_fields **trailers) {
    size_t cap = 0;
    *body = NULL;
    *len = 0;
    *trailers = NULL;
    for (;;) {
        uint8_t *line = NULL;
        size_t n = 0;
        if (fusedwasi_p2_http_line(r, err, &line, &n)) return 1;
        size_t k = 0;
        while (k < n && line[k] != ';') k++;
        while (k > 0 && (line[k - 1] == ' ' || line[k - 1] == '\t')) k--;
        size_t start = 0;
        while (start < k && (line[start] == ' ' || line[start] == '\t')) start++;
        size_t digits = k - start;
        uint64_t size = 0;
        int ok = digits >= 1 && digits <= 16;
        for (size_t i = start; ok && i < k; i++) {
            if (!fusedwasi_p2_http_is_hex(line[i])) ok = 0;
            else size = size * 16 + (uint64_t)(line[i] <= '9' ? line[i] - '0' : (line[i] | 32) - 'a' + 10);
        }
        free(line);
        if (!ok) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_PROTOCOL_ERROR);
        if (size == 0) break;
        if (*len + size > FUSEDWASI_P2_HTTP_MAX_BODY) return fusedwasi_p2_http_fail_num(err, FUSEDWASI_P2_HTTP_RESPONSE_BODY_SIZE, *len + size);
        uint8_t *chunk, *crlf;
        if (fusedwasi_p2_http_exact(r, err, (size_t)size, &chunk)) return 1;
        int bad = fusedwasi_p2_http_append(body, len, &cap, chunk, (size_t)size);
        free(chunk);
        if (bad) return FUSEDWASI_P2_HTTP_OOM(err);
        if (fusedwasi_p2_http_exact(r, err, 2, &crlf)) return 1;
        int crlf_ok = crlf[0] == '\r' && crlf[1] == '\n';
        free(crlf);
        if (!crlf_ok) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_PROTOCOL_ERROR);
    }
    fusedwasi_p2_http_fields *t = fusedwasi_p2_http_fields_new(1);
    if (!t) return FUSEDWASI_P2_HTTP_OOM(err);
    for (;;) {
        uint8_t *line = NULL;
        size_t n = 0;
        if (fusedwasi_p2_http_line(r, err, &line, &n)) {
            fusedwasi_p2_http_fields_unref(t);
            return 1;
        }
        if (n == 0) {
            free(line);
            break;
        }
        int bad = fusedwasi_p2_http_parse_field(t, line, n, err);
        free(line);
        if (bad) {
            fusedwasi_p2_http_fields_unref(t);
            return 1;
        }
    }
    if (t->count) *trailers = t;
    else fusedwasi_p2_http_fields_unref(t);
    return 0;
}

/* the body of an answer by what its head says. Returns 0, or 1 with err set. */
FUSEDWASI_P2_FN int fusedwasi_p2_http_read_body(fusedwasi_p2_http_reader *r, fusedwasi_p2_http_error *err, const char *method, uint16_t status, const fusedwasi_p2_http_fields *headers,
                                      uint8_t **body, size_t *len, fusedwasi_p2_http_fields **trailers) {
    *body = NULL;
    *len = 0;
    *trailers = NULL;
    if (strcmp(method, "HEAD") == 0 || status == 204 || status == 304) return 0;
    /* Transfer-Encoding: the codings, lower case, joined with ", " */
    char coding[512];
    size_t clen = 0;
    int count = 0, chunked_only = 1;
    coding[0] = 0;
    for (uint32_t i = 0; i < headers->count; i++) {
        if (strcmp(headers->e[i].name, "transfer-encoding") != 0) continue;
        const uint8_t *v = headers->e[i].value, *end = v + headers->e[i].len;
        while (v <= end) {
            const uint8_t *comma = v;
            while (comma < end && *comma != ',') comma++;
            const uint8_t *a = v, *b = comma;
            while (a < b && (*a == ' ' || *a == '\t')) a++;
            while (b > a && (b[-1] == ' ' || b[-1] == '\t')) b--;
            size_t n = (size_t)(b - a);
            if (count > 0 && clen + 2 < sizeof coding) coding[clen++] = ',', coding[clen++] = ' ';
            for (size_t k = 0; k < n && clen + 1 < sizeof coding; k++) coding[clen++] = (char)fusedwasi_p2_http_lower(a[k]);
            coding[clen] = 0;
            if (!(n == 7 && memcmp(coding + clen - 7, "chunked", 7) == 0)) chunked_only = 0;
            count++;
            v = comma + 1;
        }
    }
    if (count) {
        if (count != 1 || !chunked_only) return fusedwasi_p2_http_fail_str(err, FUSEDWASI_P2_HTTP_RESPONSE_TRANSFER_CODING, 0, coding);
        return fusedwasi_p2_http_read_chunked(r, err, body, len, trailers);
    }
    const fusedwasi_p2_http_entry *first = NULL;
    int distinct = 0;
    for (uint32_t i = 0; i < headers->count; i++) {
        if (strcmp(headers->e[i].name, "content-length") != 0) continue;
        if (!first) {
            first = &headers->e[i];
            distinct = 1;
        } else if (first->len != headers->e[i].len || memcmp(first->value, headers->e[i].value, first->len) != 0) {
            distinct = 2;
        }
    }
    if (first) {
        if (distinct > 1 || first->len == 0) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_PROTOCOL_ERROR);
        uint64_t n = 0;
        for (uint32_t k = 0; k < first->len; k++) {
            if (first->value[k] < '0' || first->value[k] > '9') return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_PROTOCOL_ERROR);
            uint64_t d = (uint64_t)(first->value[k] - '0');
            n = n > (UINT64_MAX - d) / 10 ? UINT64_MAX : n * 10 + d;
        }
        if (n > FUSEDWASI_P2_HTTP_MAX_BODY) return fusedwasi_p2_http_fail_num(err, FUSEDWASI_P2_HTTP_RESPONSE_BODY_SIZE, n);
        *len = (size_t)n;
        return fusedwasi_p2_http_exact(r, err, (size_t)n, body);
    }
    /* to the end of the stream */
    size_t cap = 0;
    for (;;) {
        if (r->len > r->pos && fusedwasi_p2_http_append(body, len, &cap, r->buf + r->pos, r->len - r->pos)) return FUSEDWASI_P2_HTTP_OOM(err);
        r->pos = r->len;
        if (*len > FUSEDWASI_P2_HTTP_MAX_BODY) return fusedwasi_p2_http_fail_num(err, FUSEDWASI_P2_HTTP_RESPONSE_BODY_SIZE, FUSEDWASI_P2_HTTP_MAX_BODY);
        int m = fusedwasi_p2_http_more(r, err);
        if (m < 0) return 1;
        if (m == 0) return 0;
    }
}

/* the bytes of a request: head, then the body (in chunks, with the trailers, if trailers are given). Returns a malloc'd buffer. */
FUSEDWASI_P2_FN uint8_t *fusedwasi_p2_http_build_request(const fusedwasi_p2_http_request *req, size_t *out_len) {
    uint8_t *buf = NULL;
    size_t len = 0, cap = 0;
    char tmp[64];
    int bad = 0;
#define FUSEDWASI_P2_PUT(p, n) (bad |= fusedwasi_p2_http_append(&buf, &len, &cap, (const uint8_t *)(p), (n)))
#define FUSEDWASI_P2_PUTS(t) FUSEDWASI_P2_PUT((t), strlen(t))
    FUSEDWASI_P2_PUTS(req->method), FUSEDWASI_P2_PUTS(" "), FUSEDWASI_P2_PUTS(req->path), FUSEDWASI_P2_PUTS(" HTTP/1.1\r\nHost: "), FUSEDWASI_P2_PUTS(req->authority), FUSEDWASI_P2_PUTS("\r\n");
    int has_length = 0;
    for (uint32_t i = 0; i < req->headers->count; i++) {
        FUSEDWASI_P2_PUTS(req->headers->e[i].name), FUSEDWASI_P2_PUTS(": "), FUSEDWASI_P2_PUT(req->headers->e[i].value, req->headers->e[i].len), FUSEDWASI_P2_PUTS("\r\n");
        if (strcmp(req->headers->e[i].name, "content-length") == 0) has_length = 1;
    }
    if (req->trailers) {
        FUSEDWASI_P2_PUTS("transfer-encoding: chunked\r\n");
    } else if (!has_length && (req->body_len || strcmp(req->method, "POST") == 0 || strcmp(req->method, "PUT") == 0 || strcmp(req->method, "PATCH") == 0)) {
        snprintf(tmp, sizeof tmp, "content-length: %llu\r\n", (unsigned long long)req->body_len);
        FUSEDWASI_P2_PUTS(tmp);
    }
    FUSEDWASI_P2_PUTS("connection: close\r\n\r\n");
    if (!req->trailers) {
        FUSEDWASI_P2_PUT(req->body, (size_t)req->body_len);
    } else {
        if (req->body_len) {
            snprintf(tmp, sizeof tmp, "%llx\r\n", (unsigned long long)req->body_len);
            FUSEDWASI_P2_PUTS(tmp), FUSEDWASI_P2_PUT(req->body, (size_t)req->body_len), FUSEDWASI_P2_PUTS("\r\n");
        }
        FUSEDWASI_P2_PUTS("0\r\n");
        for (uint32_t i = 0; i < req->trailers->count; i++)
            FUSEDWASI_P2_PUTS(req->trailers->e[i].name), FUSEDWASI_P2_PUTS(": "), FUSEDWASI_P2_PUT(req->trailers->e[i].value, req->trailers->e[i].len), FUSEDWASI_P2_PUTS("\r\n");
        FUSEDWASI_P2_PUTS("\r\n");
    }
#undef FUSEDWASI_P2_PUT
#undef FUSEDWASI_P2_PUTS
    if (bad) {
        free(buf);
        return NULL;
    }
    *out_len = len;
    return buf;
}

/* The transport of the host: a connection for each request, `Connection: close`; plain http only. */
FUSEDWASI_P2_FN int fusedwasi_p2_http_default_send(fusedwasi_p2_state *state, const fusedwasi_p2_http_request *req, fusedwasi_p2_http_response *out, fusedwasi_p2_http_error *err) {
    (void)state;
    int https = strcmp(req->scheme, "https") == 0;
    if (https) return fusedwasi_p2_http_fail_str(err, FUSEDWASI_P2_HTTP_INTERNAL_ERROR, 0, "https needs a transport of the embedder (`http_send` of the state): this host has no TLS");
    char *host = NULL;
    int port = 0;
    if (!fusedwasi_p2_http_split_authority(req->authority, strlen(req->authority), 80, &host, &port)) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_REQUEST_URI_INVALID);
    int fd = fusedwasi_p2_http_connect(host, port, fusedwasi_p2_http_ms(req, 0), err);
    free(host);
    if (fd < 0) return 1;
    int rc = 1;
    size_t n = 0, sent = 0;
    uint8_t *bytes = fusedwasi_p2_http_build_request(req, &n);
    fusedwasi_p2_http_reader r;
    memset(&r, 0, sizeof r);
    r.fd = fd;
    r.first = 1;
    r.first_ms = fusedwasi_p2_http_ms(req, 1);
    r.between_ms = fusedwasi_p2_http_ms(req, 2);
    fusedwasi_p2_http_fields *headers = NULL, *trailers = NULL;
    uint8_t *body = NULL;
    size_t body_len = 0;
    uint16_t status = 0;
    if (!bytes) {
        FUSEDWASI_P2_HTTP_OOM(err);
        goto done;
    }
    while (sent < n) {
        int w = fusedwasi_p2_http_wait_fd(fd, POLLOUT, r.between_ms);
        if (w == 0) {
            fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_CONNECTION_WRITE_TIMEOUT);
            goto done;
        }
        fusedwasi_p2_ssize k = w < 0 ? -1 : FUSEDWASI_P2_SEND(fd, bytes + sent, n - sent, 0);
        if (k < 0 && fusedwasi_p2_errno() == EINTR) continue;
        if (k < 0) {
            fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_CONNECTION_TERMINATED);
            goto done;
        }
        sent += (size_t)k;
    }
    headers = fusedwasi_p2_http_fields_new(1);
    if (!headers) {
        FUSEDWASI_P2_HTTP_OOM(err);
        goto done;
    }
    headers->immutable = 0; /* filled below, then made immutable */
    if (fusedwasi_p2_http_read_head(&r, err, &status, headers)) goto done;
    headers->immutable = 1;
    if (fusedwasi_p2_http_read_body(&r, err, req->method, status, headers, &body, &body_len, &trailers)) goto done;
    fusedwasi_p2_http_blob *blob = fusedwasi_p2_http_blob_new(body, body_len);
    if (!blob) {
        FUSEDWASI_P2_HTTP_OOM(err);
        goto done;
    }
    {  /* the framing was undone here, so the guest is not shown it (as in Wasmtime) */
        static const uint8_t te[] = "transfer-encoding";
        fusedwasi_p2_http_fields_remove(headers, te, sizeof te - 1);
    }
    out->status = status;
    out->headers = headers;
    out->body = blob;
    out->trailers = trailers;
    headers = NULL;
    trailers = NULL;
    rc = 0;
done:
    FUSEDWASI_P2_CLOSE(fd);
    free(bytes);
    free(r.buf);
    free(body);
    fusedwasi_p2_http_fields_unref(headers);
    fusedwasi_p2_http_fields_unref(trailers);
    return rc;
}



/* the answer */

typedef struct {
    uint16_t status;
    fusedwasi_p2_http_fields *headers;
    fusedwasi_p2_http_payload payload;
} fusedwasi_p2_http_iresponse;

FUSEDWASI_P2_FN void fusedwasi_p2_http_iresponse_free(fusedwasi_p2_http_iresponse *r) {
    if (!r) return;
    fusedwasi_p2_http_fields_unref(r->headers);
    fusedwasi_p2_http_payload_free(&r->payload);
    free(r);
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_destroy_iresponse(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    fusedwasi_p2_http_iresponse_free((fusedwasi_p2_http_iresponse *)obj);
}

typedef struct fusedwasi_p2_http_future {
    int refs;
    fusedwasi_p2_http_orequest *request;
    fusedwasi_p2_http_options options;
    char *scheme, *path;
    int done, taken, failed;
    fusedwasi_p2_http_error error;
    fusedwasi_p2_http_iresponse *response;
} fusedwasi_p2_http_future;

FUSEDWASI_P2_FN void fusedwasi_p2_http_future_unref(fusedwasi_p2_http_future *f) {
    if (--f->refs > 0) return;
    fusedwasi_p2_http_orequest_free(f->request);
    free(f->scheme), free(f->path);
    fusedwasi_p2_http_error_free(&f->error);
    fusedwasi_p2_http_iresponse_free(f->response);
    free(f);
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_destroy_future(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    fusedwasi_p2_http_future_unref((fusedwasi_p2_http_future *)obj);
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_future_fail(fusedwasi_p2_http_future *f, uint32_t tag) {
    fusedwasi_p2_http_fail(&f->error, tag);
    f->failed = 1;
}

/* Sends the request if it is time to. Returns 1 when the answer (or the failure) is there, 0 when not yet, -1 when there is no memory to keep the answer. */
FUSEDWASI_P2_FN int fusedwasi_p2_http_future_advance(fusedwasi_p2_state *state, fusedwasi_p2_http_future *f) {
    if (f->done) return 1;
    fusedwasi_p2_http_obody *body = f->request->body;
    if (body && !(body->finished || body->abandoned)) return 0;
    f->done = 1;
    if (body && body->abandoned) {
        fusedwasi_p2_http_future_fail(f, FUSEDWASI_P2_HTTP_PROTOCOL_ERROR);
        return 1;
    }
    fusedwasi_p2_http_request req;
    memset(&req, 0, sizeof req);
    req.method = f->request->method;
    req.scheme = f->scheme;
    req.authority = f->request->authority;
    req.path = f->path;
    req.headers = f->request->headers;
    if (body) {
        req.body = body->buf;
        req.body_len = body->len;
        req.trailers = body->trailers;
    }
    for (int i = 0; i < 3; i++) req.has_timeout[i] = f->options.has[i], req.timeout_ns[i] = f->options.ns[i];
    fusedwasi_p2_http_response resp;
    memset(&resp, 0, sizeof resp);
    fusedwasi_p2_http_error err;
    fusedwasi_p2_http_error_init(&err, 0);
    int rc = (state->http_send ? state->http_send : fusedwasi_p2_http_default_send)(state, &req, &resp, &err);
    if (rc != 0) {
        fusedwasi_p2_http_error_free(&f->error);
        f->error = err;
        f->failed = 1;
        return 1;
    }
    fusedwasi_p2_http_error_free(&err);
    fusedwasi_p2_http_iresponse *r = (fusedwasi_p2_http_iresponse *)calloc(1, sizeof *r);
    if (!r) return -1;
    r->status = resp.status;
    r->headers = resp.headers;
    r->headers->immutable = 1;
    r->payload.blob = resp.body;
    r->payload.trailers = resp.trailers;
    if (r->payload.trailers) r->payload.trailers->immutable = 1;
    f->response = r;
    return 1;
}

FUSEDWASI_P2_FN int fusedwasi_p2_http_future_ready(fusedwasi_p2_state *state, fusedwasi_p2_pollable *p) {
    int r = fusedwasi_p2_http_future_advance(state, (fusedwasi_p2_http_future *)p->ctx);
    if (r < 0) p->failure = "out of memory";
    return r;
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_destroy_future_pollable(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    fusedwasi_p2_pollable *p = (fusedwasi_p2_pollable *)obj;
    fusedwasi_p2_http_future_unref((fusedwasi_p2_http_future *)p->ctx);
    free(p);
}

FUSEDWASI_P2_FN fusedwasi_p2_http_future *fusedwasi_p2_http_future_get(fusedwasi_p2_guest *g, uint32_t handle) {
    return (fusedwasi_p2_http_future *)fusedwasi_p2_table_get(g, handle, FUSEDWASI_P2_KIND_FUTURE_INCOMING_RESPONSE)->obj;
}

/* the request as the transport takes it, or the failure (also the policy). Returns 0 or 1 with err set. */
FUSEDWASI_P2_FN int fusedwasi_p2_http_prepare(fusedwasi_p2_guest *g, fusedwasi_p2_http_orequest *r, fusedwasi_p2_http_future *f, fusedwasi_p2_http_error *err) {
    if (!fusedwasi_p2_http_is_token((const uint8_t *)r->method, strlen(r->method))) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_REQUEST_METHOD_INVALID);
    const char *scheme = r->scheme ? r->scheme : "https";
    if ((strcmp(scheme, "http") != 0 && strcmp(scheme, "https") != 0) || !r->authority) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_REQUEST_URI_INVALID);
    if (!fusedwasi_p2_http_split_authority(r->authority, strlen(r->authority), 80, NULL, NULL)) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_REQUEST_URI_INVALID);
    if (!g->state->http_check || !g->state->http_check(g->state, scheme, r->authority)) return fusedwasi_p2_http_fail(err, FUSEDWASI_P2_HTTP_REQUEST_DENIED);
    f->scheme = fusedwasi_p2_http_dup(scheme, strlen(scheme));
    f->path = fusedwasi_p2_http_dup(r->path ? r->path : "/", strlen(r->path ? r->path : "/"));
    if (!f->scheme || !f->path) g->trap(g, "out of memory");
    return 0;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_outgoing_handler_handle(fusedwasi_p2_guest *g, uint32_t comp, uint32_t request, uint32_t opt_tag, uint32_t opt_handle, uint32_t retptr) {
    fusedwasi_p2_http_orequest_get(g, request);
    fusedwasi_p2_http_future *f = (fusedwasi_p2_http_future *)calloc(1, sizeof *f);
    if (!f) g->trap(g, "out of memory");
    f->refs = 1;
    if (opt_tag) f->options = *fusedwasi_p2_http_options_get(g, opt_handle);
    f->request = (fusedwasi_p2_http_orequest *)fusedwasi_p2_table_take(g, request, FUSEDWASI_P2_KIND_OUTGOING_REQUEST);
    if (opt_tag) fusedwasi_p2_table_drop(g, opt_handle, FUSEDWASI_P2_KIND_REQUEST_OPTIONS);
    fusedwasi_p2_http_error err;
    fusedwasi_p2_http_error_init(&err, 0);
    if (fusedwasi_p2_http_prepare(g, f->request, f, &err)) {
        fusedwasi_p2_http_future_unref(f);
        fusedwasi_p2_store_u8(g, comp, retptr, 1);
        fusedwasi_p2_http_store_error(g, comp, retptr + 8, &err);
        fusedwasi_p2_http_error_free(&err);
        return;
    }
    fusedwasi_p2_http_error_free(&err);
    fusedwasi_p2_store_u8(g, comp, retptr, 0);
    fusedwasi_p2_store_u32(g, comp, retptr + 8, fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_FUTURE_INCOMING_RESPONSE, f, fusedwasi_p2_http_destroy_future));
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_future_incoming_response_subscribe(fusedwasi_p2_guest *g, uint32_t self) {
    fusedwasi_p2_http_future *f = fusedwasi_p2_http_future_get(g, self);
    fusedwasi_p2_pollable *p = (fusedwasi_p2_pollable *)calloc(1, sizeof *p);
    if (!p) g->trap(g, "out of memory");
    p->ready = fusedwasi_p2_http_future_ready;
    p->ctx = f;
    f->refs++;
    return fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_POLLABLE, p, fusedwasi_p2_http_destroy_future_pollable);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_future_incoming_response_get(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_future *f = fusedwasi_p2_http_future_get(g, self);
    int advanced = fusedwasi_p2_http_future_advance(g->state, f);
    if (advanced < 0) g->trap(g, "out of memory");
    if (!advanced) {
        fusedwasi_p2_store_u8(g, comp, retptr, 0);
        return;
    }
    fusedwasi_p2_store_u8(g, comp, retptr, 1);
    if (f->taken) {
        fusedwasi_p2_store_u8(g, comp, retptr + 8, 1);
        return;
    }
    f->taken = 1;
    fusedwasi_p2_store_u8(g, comp, retptr + 8, 0);
    if (f->failed) {
        fusedwasi_p2_store_u8(g, comp, retptr + 16, 1);
        fusedwasi_p2_http_store_error(g, comp, retptr + 24, &f->error);
    } else {
        fusedwasi_p2_store_u8(g, comp, retptr + 16, 0);
        fusedwasi_p2_store_u32(g, comp, retptr + 24, fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_INCOMING_RESPONSE, f->response, fusedwasi_p2_http_destroy_iresponse));
        f->response = NULL;
    }
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_future_incoming_response_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_http_future_get(g, handle);
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_FUTURE_INCOMING_RESPONSE);
}

FUSEDWASI_P2_FN fusedwasi_p2_http_iresponse *fusedwasi_p2_http_iresponse_get(fusedwasi_p2_guest *g, uint32_t handle) {
    return (fusedwasi_p2_http_iresponse *)fusedwasi_p2_table_get(g, handle, FUSEDWASI_P2_KIND_INCOMING_RESPONSE)->obj;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_incoming_response_status(fusedwasi_p2_guest *g, uint32_t self) { return fusedwasi_p2_http_iresponse_get(g, self)->status; }

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_incoming_response_headers(fusedwasi_p2_guest *g, uint32_t self) {
    fusedwasi_p2_http_fields *f = fusedwasi_p2_http_iresponse_get(g, self)->headers;
    fusedwasi_p2_http_fields_ref(f);
    return fusedwasi_p2_http_fields_handle(g, f);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_incoming_response_consume(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_consume(g, comp, &fusedwasi_p2_http_iresponse_get(g, self)->payload, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_incoming_response_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_http_iresponse_get(g, handle);
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_INCOMING_RESPONSE);
}

/* the server side: what an embedder gives a component that exports incoming-handler, and what it gets back */

typedef struct {
    char *method, *path, *scheme, *authority;
    fusedwasi_p2_http_fields *headers;
    fusedwasi_p2_http_payload payload;
} fusedwasi_p2_http_irequest;

FUSEDWASI_P2_FN void fusedwasi_p2_http_destroy_irequest(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    fusedwasi_p2_http_irequest *r = (fusedwasi_p2_http_irequest *)obj;
    free(r->method), free(r->path), free(r->scheme), free(r->authority);
    fusedwasi_p2_http_fields_unref(r->headers);
    fusedwasi_p2_http_payload_free(&r->payload);
    free(r);
}

/* A request for the guests of a state (its handle in the table of the state; 0 when there is no memory or the table is full, nothing is kept then). `path`, `scheme` and `authority` may be NULL; `scheme` is "http", "https" or another text. The strings and the body are copied; `headers`
   and `trailers` (may be NULL) are fields the caller made with fusedwasi_p2_http_fields_new: the request takes a reference of its own and makes them immutable. */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2_http_incoming_request_new(fusedwasi_p2_state *state, const char *method, const char *path, const char *scheme, const char *authority,
                                                      fusedwasi_p2_http_fields *headers, const void *body, uint64_t body_len, fusedwasi_p2_http_fields *trailers) {
    fusedwasi_p2_http_irequest *r = (fusedwasi_p2_http_irequest *)calloc(1, sizeof *r);
    if (!r) return 0;
    r->method = fusedwasi_p2_http_dup(method, strlen(method));
    r->path = path ? fusedwasi_p2_http_dup(path, strlen(path)) : NULL;
    r->scheme = scheme ? fusedwasi_p2_http_dup(scheme, strlen(scheme)) : NULL;
    r->authority = authority ? fusedwasi_p2_http_dup(authority, strlen(authority)) : NULL;
    r->headers = headers ? headers : fusedwasi_p2_http_fields_new(1);
    if (headers) fusedwasi_p2_http_fields_ref(headers);
    r->headers->immutable = 1;
    r->payload.blob = fusedwasi_p2_http_blob_new(body, body_len);
    r->payload.trailers = trailers;
    if (trailers) fusedwasi_p2_http_fields_ref(trailers), trailers->immutable = 1;
    uint32_t handle = fusedwasi_p2_table_add(state, FUSEDWASI_P2_KIND_INCOMING_REQUEST, r, fusedwasi_p2_http_destroy_irequest);
    if (!handle) fusedwasi_p2_http_destroy_irequest(state, r); /* releases what the request took */
    return handle;
}

FUSEDWASI_P2_FN fusedwasi_p2_http_irequest *fusedwasi_p2_http_irequest_get(fusedwasi_p2_guest *g, uint32_t handle) {
    return (fusedwasi_p2_http_irequest *)fusedwasi_p2_table_get(g, handle, FUSEDWASI_P2_KIND_INCOMING_REQUEST)->obj;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_incoming_request_method(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_store_method(g, comp, retptr, fusedwasi_p2_http_irequest_get(g, self)->method);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_incoming_request_path_with_query(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_store_opt_string(g, comp, retptr, fusedwasi_p2_http_irequest_get(g, self)->path);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_incoming_request_scheme(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_store_opt_scheme(g, comp, retptr, fusedwasi_p2_http_irequest_get(g, self)->scheme);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_incoming_request_authority(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_store_opt_string(g, comp, retptr, fusedwasi_p2_http_irequest_get(g, self)->authority);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_incoming_request_headers(fusedwasi_p2_guest *g, uint32_t self) {
    fusedwasi_p2_http_fields *f = fusedwasi_p2_http_irequest_get(g, self)->headers;
    fusedwasi_p2_http_fields_ref(f);
    return fusedwasi_p2_http_fields_handle(g, f);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_incoming_request_consume(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_consume(g, comp, &fusedwasi_p2_http_irequest_get(g, self)->payload, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_incoming_request_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_http_irequest_get(g, handle);
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_INCOMING_REQUEST);
}

typedef struct fusedwasi_p2_http_oresponse {
    int refs;
    uint16_t status;
    fusedwasi_p2_http_fields *headers;
    fusedwasi_p2_http_obody *body; /* the bytes written so far: buf, len; `finished`, `trailers` */
} fusedwasi_p2_http_oresponse;

FUSEDWASI_P2_FN void fusedwasi_p2_http_oresponse_unref(fusedwasi_p2_http_oresponse *r) {
    if (!r || --r->refs > 0) return;
    fusedwasi_p2_http_fields_unref(r->headers);
    fusedwasi_p2_http_obody_unref(r->body);
    free(r);
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_destroy_oresponse(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    fusedwasi_p2_http_oresponse_unref((fusedwasi_p2_http_oresponse *)obj);
}

/* where the guest puts its answer: `response` or `error` (`failed`) is set when it calls `response-outparam.set`, or when the handle is dropped without it */
typedef struct fusedwasi_p2_http_outparam {
    int refs;
    int done, failed;
    fusedwasi_p2_http_oresponse *response;
    fusedwasi_p2_http_error error;
} fusedwasi_p2_http_outparam;

FUSEDWASI_P2_FN void fusedwasi_p2_http_outparam_release(fusedwasi_p2_http_outparam *p) {
    if (!p || --p->refs > 0) return;
    fusedwasi_p2_http_oresponse_unref(p->response);
    fusedwasi_p2_http_error_free(&p->error);
    free(p);
}

FUSEDWASI_P2_FN void fusedwasi_p2_http_destroy_outparam(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    fusedwasi_p2_http_outparam *p = (fusedwasi_p2_http_outparam *)obj;
    if (!p->done) {
        fusedwasi_p2_http_fail_str(&p->error, FUSEDWASI_P2_HTTP_INTERNAL_ERROR, 0, "response-outparam dropped without a response");
        p->failed = 1;
        p->done = 1;
    }
    fusedwasi_p2_http_outparam_release(p);
}

/* makes the place for an answer; returns it (the caller releases it with fusedwasi_p2_http_outparam_release) and gives the handle for the guest. NULL (and `*handle` 0) when there is
   no memory or the table is full. */
FUSEDWASI_P2_FN fusedwasi_p2_http_outparam *fusedwasi_p2_http_outparam_new(fusedwasi_p2_state *state, uint32_t *handle) {
    fusedwasi_p2_http_outparam *p = (fusedwasi_p2_http_outparam *)calloc(1, sizeof *p);
    *handle = 0;
    if (!p) return NULL;
    p->refs = 2; /* the caller and the handle */
    *handle = fusedwasi_p2_table_add(state, FUSEDWASI_P2_KIND_RESPONSE_OUTPARAM, p, fusedwasi_p2_http_destroy_outparam);
    if (!*handle) {
        free(p);
        return NULL;
    }
    return p;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_response_outparam_set(fusedwasi_p2_guest *g, uint32_t comp, uint32_t param, uint32_t tag, uint32_t a1, uint32_t a2, uint64_t a3,
                                                      uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    fusedwasi_p2_http_outparam *p = (fusedwasi_p2_http_outparam *)fusedwasi_p2_table_get(g, param, FUSEDWASI_P2_KIND_RESPONSE_OUTPARAM)->obj;
    if (tag == 0) {
        fusedwasi_p2_table_get(g, (uint32_t)a1, FUSEDWASI_P2_KIND_OUTGOING_RESPONSE);
        p->response = (fusedwasi_p2_http_oresponse *)fusedwasi_p2_table_take(g, (uint32_t)a1, FUSEDWASI_P2_KIND_OUTGOING_RESPONSE);
    } else if (tag == 1) {
        uint64_t flat[7] = {a1, a2, a3, a4, a5, a6, a7};
        fusedwasi_p2_http_lift_error(g, comp, flat, &p->error);
        p->failed = 1;
    } else {
        g->trap(g, "bad result");
    }
    p->done = 1;
    fusedwasi_p2_table_drop(g, param, FUSEDWASI_P2_KIND_RESPONSE_OUTPARAM);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_response_outparam_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_table_get(g, handle, FUSEDWASI_P2_KIND_RESPONSE_OUTPARAM);
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_RESPONSE_OUTPARAM);
}

FUSEDWASI_P2_FN fusedwasi_p2_http_oresponse *fusedwasi_p2_http_oresponse_get(fusedwasi_p2_guest *g, uint32_t handle) {
    return (fusedwasi_p2_http_oresponse *)fusedwasi_p2_table_get(g, handle, FUSEDWASI_P2_KIND_OUTGOING_RESPONSE)->obj;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_outgoing_response_new(fusedwasi_p2_guest *g, uint32_t headers) {
    fusedwasi_p2_http_oresponse *r = (fusedwasi_p2_http_oresponse *)calloc(1, sizeof *r);
    if (!r) g->trap(g, "out of memory");
    r->refs = 1;
    r->status = 200;
    r->headers = fusedwasi_p2_http_fields_take(g, headers);
    return fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_OUTGOING_RESPONSE, r, fusedwasi_p2_http_destroy_oresponse);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_outgoing_response_status_code(fusedwasi_p2_guest *g, uint32_t self) { return fusedwasi_p2_http_oresponse_get(g, self)->status; }

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_outgoing_response_set_status_code(fusedwasi_p2_guest *g, uint32_t self, uint32_t status) {
    fusedwasi_p2_http_oresponse *r = fusedwasi_p2_http_oresponse_get(g, self);
    if (status < 100 || status > 599) return 1;
    r->status = (uint16_t)status;
    return 0;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_http_types_outgoing_response_headers(fusedwasi_p2_guest *g, uint32_t self) {
    fusedwasi_p2_http_fields *f = fusedwasi_p2_http_oresponse_get(g, self)->headers;
    fusedwasi_p2_http_fields_ref(f);
    return fusedwasi_p2_http_fields_handle(g, f);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_outgoing_response_body(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_http_oresponse *r = fusedwasi_p2_http_oresponse_get(g, self);
    fusedwasi_p2_http_new_body(g, comp, r->headers, 1, &r->body, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_outgoing_response_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_http_oresponse_get(g, handle);
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_OUTGOING_RESPONSE);
}

/* errors of the io resource: a stream of a body never fails here, so no error carries an http error-code */
FUSEDWASI_P2_FN void fusedwasi_wasi_http_types_http_error_code(fusedwasi_p2_guest *g, uint32_t comp, uint32_t err, uint32_t retptr) {
    fusedwasi_p2_table_get(g, err, FUSEDWASI_P2_KIND_ERROR);
    fusedwasi_p2_store_u8(g, comp, retptr, 0);
}

#endif /* FUSEDWASI_P2_HTTP_H */
