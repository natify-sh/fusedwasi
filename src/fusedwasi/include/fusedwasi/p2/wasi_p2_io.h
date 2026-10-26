/* wasi:io (23 imports): error, poll and streams (the twin of p2/wasi_p2_io.py; the layouts of the results are described there).
 *
 * A stream of the host is a `fusedwasi_p2_stream` with the functions it needs (an input stream: read; an output stream: check_write, write, flush); `ready` is for pollables.
 * `read` and the others return FUSEDWASI_P2_STREAM_OK, FUSEDWASI_P2_STREAM_CLOSED (the end) or FUSEDWASI_P2_STREAM_FAILED (then `failure` is the message). A write of more than
 * check_write allows is a trap. `fusedwasi_p2_memory_input` and `fusedwasi_p2_buffer_output` are here; files and sockets come with their packages. */
#ifndef FUSEDWASI_P2_IO_H
#define FUSEDWASI_P2_IO_H

#include "wasi_p2_clocks.h" /* the deadline pollables share the table and fusedwasi_p2_free_obj */

#define FUSEDWASI_P2_READ_CAP 65536u

enum { FUSEDWASI_P2_STREAM_OK = 0, FUSEDWASI_P2_STREAM_CLOSED = 1, FUSEDWASI_P2_STREAM_FAILED = 2 };

typedef struct fusedwasi_p2_stream {
    int (*read)(fusedwasi_p2_state *s, struct fusedwasi_p2_stream *st, uint8_t *buf, uint64_t len, uint64_t *got);
    uint64_t (*check_write)(fusedwasi_p2_state *s, struct fusedwasi_p2_stream *st, int *status);
    int (*write)(fusedwasi_p2_state *s, struct fusedwasi_p2_stream *st, const uint8_t *data, uint64_t len);
    int (*flush)(fusedwasi_p2_state *s, struct fusedwasi_p2_stream *st);
    int (*ready)(fusedwasi_p2_state *s, struct fusedwasi_p2_stream *st);
    void (*destroy)(fusedwasi_p2_state *s, struct fusedwasi_p2_stream *st); /* when the handle is dropped; may be NULL */
    const char *failure;
    int failure_has_code, failure_code; /* the error-code of wasi:filesystem, when the failure came from a file */
    int failure_network_code;           /* the error-code of wasi:sockets, when it came from a socket; -1 none (set it with the failure: calloc gives 0, so see failure_has_network) */
    int failure_has_network;
    int failure_errno;                  /* the errno of the system for a FAILED (0: not known); WASI 0.1 gives it to the program as it is, 0.2 has only the message */
    int dead;                           /* set when an operation failed: from then on every call is `closed` (the rule of stream-error) */
    void *ctx;
} fusedwasi_p2_stream;

/* the `error` resource: a code (-1: none) and the message */
typedef struct {
    int code;         /* wasi:filesystem error-code, -1 none */
    int network_code; /* wasi:sockets error-code, -1 none */
    char message[1];
} fusedwasi_p2_error;

/* The calls of a stream under the rule of `stream-error`: after FAILED the stream is closed. */
FUSEDWASI_P2_FN int fusedwasi_p2_s_read(fusedwasi_p2_state *s, fusedwasi_p2_stream *st, uint8_t *buf, uint64_t len, uint64_t *got) {
    *got = 0;
    if (st->dead) return FUSEDWASI_P2_STREAM_CLOSED;
    int r = st->read(s, st, buf, len, got);
    if (r == FUSEDWASI_P2_STREAM_FAILED) st->dead = 1;
    return r;
}

FUSEDWASI_P2_FN uint64_t fusedwasi_p2_s_check_write(fusedwasi_p2_state *s, fusedwasi_p2_stream *st, int *status) {
    if (st->dead) {
        *status = FUSEDWASI_P2_STREAM_CLOSED;
        return 0;
    }
    uint64_t n = st->check_write(s, st, status);
    if (*status == FUSEDWASI_P2_STREAM_FAILED) st->dead = 1;
    return n;
}

FUSEDWASI_P2_FN int fusedwasi_p2_s_write(fusedwasi_p2_state *s, fusedwasi_p2_stream *st, const uint8_t *data, uint64_t len) {
    if (st->dead) return FUSEDWASI_P2_STREAM_CLOSED;
    int r = st->write(s, st, data, len);
    if (r == FUSEDWASI_P2_STREAM_FAILED) st->dead = 1;
    return r;
}

FUSEDWASI_P2_FN int fusedwasi_p2_s_flush(fusedwasi_p2_state *s, fusedwasi_p2_stream *st) {
    if (st->dead) return FUSEDWASI_P2_STREAM_CLOSED;
    int r = st->flush(s, st);
    if (r == FUSEDWASI_P2_STREAM_FAILED) st->dead = 1;
    return r;
}

/* making resources for the guest */

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_error_new_codes(fusedwasi_p2_state *s, const char *message, int code, int network_code) {
    size_t n = strlen(message) + 1;
    fusedwasi_p2_error *e = (fusedwasi_p2_error *)malloc(sizeof *e + n);
    if (!e) s->trap(s, "out of memory");
    e->code = code;
    e->network_code = network_code;
    memcpy(e->message, message, n);
    return fusedwasi_p2_table_new(s, FUSEDWASI_P2_KIND_ERROR, e, fusedwasi_p2_free_obj);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_error_new_code(fusedwasi_p2_state *s, const char *message, int code) { return fusedwasi_p2_error_new_codes(s, message, code, -1); }

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_error_new(fusedwasi_p2_state *s, const char *message) { return fusedwasi_p2_error_new_code(s, message, -1); }

FUSEDWASI_P2_FN void fusedwasi_p2_stream_destroy(fusedwasi_p2_state *s, void *obj) {
    fusedwasi_p2_stream *st = (fusedwasi_p2_stream *)obj;
    if (st->destroy) st->destroy(s, st);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_input_stream_new(fusedwasi_p2_state *s, fusedwasi_p2_stream *st) {
    return fusedwasi_p2_table_new(s, FUSEDWASI_P2_KIND_INPUT_STREAM, st, fusedwasi_p2_stream_destroy);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_output_stream_new(fusedwasi_p2_state *s, fusedwasi_p2_stream *st) {
    return fusedwasi_p2_table_new(s, FUSEDWASI_P2_KIND_OUTPUT_STREAM, st, fusedwasi_p2_stream_destroy);
}

/* a stream over bytes it was given (not copied; they must outlive the stream) */
typedef struct {
    fusedwasi_p2_stream base;
    const uint8_t *data;
    uint64_t len, pos;
} fusedwasi_p2_memory_input;

FUSEDWASI_P2_FN int fusedwasi_p2_memory_input_read(fusedwasi_p2_state *s, fusedwasi_p2_stream *st, uint8_t *buf, uint64_t len, uint64_t *got) {
    fusedwasi_p2_memory_input *m = (fusedwasi_p2_memory_input *)st;
    (void)s;
    if (m->pos >= m->len && len) return FUSEDWASI_P2_STREAM_CLOSED;
    uint64_t n = m->len - m->pos < len ? m->len - m->pos : len;
    memcpy(buf, m->data + m->pos, (size_t)n);
    m->pos += n;
    *got = n;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_always_ready(fusedwasi_p2_state *s, fusedwasi_p2_stream *st) {
    (void)s;
    (void)st;
    return 1;
}

FUSEDWASI_P2_FN void fusedwasi_p2_stream_free(fusedwasi_p2_state *s, fusedwasi_p2_stream *st) {
    (void)s;
    free(st);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_memory_input_new(fusedwasi_p2_state *s, const uint8_t *data, uint64_t len) {
    fusedwasi_p2_memory_input *m = (fusedwasi_p2_memory_input *)calloc(1, sizeof *m);
    if (!m) s->trap(s, "out of memory");
    m->base.read = fusedwasi_p2_memory_input_read;
    m->base.ready = fusedwasi_p2_always_ready;
    m->base.destroy = fusedwasi_p2_stream_free;
    m->data = data;
    m->len = len;
    return fusedwasi_p2_input_stream_new(s, &m->base);
}

/* a stream that collects what is written (up to `limit` bytes in all); freed with the state, read `buf` and `len` before that */
typedef struct {
    fusedwasi_p2_stream base;
    uint8_t *buf;
    uint64_t len, capacity, limit;
} fusedwasi_p2_buffer_output;

FUSEDWASI_P2_FN uint64_t fusedwasi_p2_buffer_output_check_write(fusedwasi_p2_state *s, fusedwasi_p2_stream *st, int *status) {
    fusedwasi_p2_buffer_output *b = (fusedwasi_p2_buffer_output *)st;
    (void)s;
    *status = FUSEDWASI_P2_STREAM_OK;
    return b->limit - b->len;
}

FUSEDWASI_P2_FN int fusedwasi_p2_buffer_output_write(fusedwasi_p2_state *s, fusedwasi_p2_stream *st, const uint8_t *data, uint64_t len) {
    fusedwasi_p2_buffer_output *b = (fusedwasi_p2_buffer_output *)st;
    if (b->len + len > b->capacity) {
        uint64_t cap = b->capacity ? b->capacity : 64;
        while (cap < b->len + len) cap *= 2;
        uint8_t *grown = (uint8_t *)realloc(b->buf, (size_t)cap);
        if (!grown) s->trap(s, "out of memory");
        b->buf = grown;
        b->capacity = cap;
    }
    if (len) memcpy(b->buf + b->len, data, (size_t)len);
    b->len += len;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_buffer_output_flush(fusedwasi_p2_state *s, fusedwasi_p2_stream *st) {
    (void)s;
    (void)st;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_buffer_output_ready(fusedwasi_p2_state *s, fusedwasi_p2_stream *st) {
    int status;
    return fusedwasi_p2_buffer_output_check_write(s, st, &status) > 0;
}

/* the buffer is released by the caller with `fusedwasi_p2_buffer_output_release` after the handle is dropped or the state freed */
FUSEDWASI_P2_FN fusedwasi_p2_buffer_output *fusedwasi_p2_buffer_output_new(fusedwasi_p2_state *s, uint64_t limit, uint32_t *handle) {
    fusedwasi_p2_buffer_output *b = (fusedwasi_p2_buffer_output *)calloc(1, sizeof *b);
    if (!b) s->trap(s, "out of memory");
    b->base.check_write = fusedwasi_p2_buffer_output_check_write;
    b->base.write = fusedwasi_p2_buffer_output_write;
    b->base.flush = fusedwasi_p2_buffer_output_flush;
    b->base.ready = fusedwasi_p2_buffer_output_ready;
    b->limit = limit;
    *handle = fusedwasi_p2_output_stream_new(s, &b->base);
    return b;
}

FUSEDWASI_P2_FN void fusedwasi_p2_buffer_output_release(fusedwasi_p2_buffer_output *b) {
    free(b->buf);
    free(b);
}

/* results */

FUSEDWASI_P2_FN void fusedwasi_p2_ok(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr) { fusedwasi_p2_store_u8(s, comp, retptr, 0); }

/* err(stream-error): the tag of the result at retptr, the variant at error_at */
FUSEDWASI_P2_FN void fusedwasi_p2_stream_err(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr, uint32_t error_at, int status, fusedwasi_p2_stream *st) {
    const char *failure = st->failure;
    fusedwasi_p2_store_u8(s, comp, retptr, 1);
    if (status == FUSEDWASI_P2_STREAM_FAILED) {
        fusedwasi_p2_store_u8(s, comp, error_at, 0);
        fusedwasi_p2_store_u32(s, comp, error_at + 4, fusedwasi_p2_error_new_codes(s, failure ? failure : "failed", st->failure_has_code ? st->failure_code : -1,
                                                                    st->failure_has_network ? st->failure_network_code : -1));
    } else {
        fusedwasi_p2_store_u8(s, comp, error_at, 1);
    }
}

/* error */

FUSEDWASI_P2_FN void fusedwasi_wasi_io_error_error_to_debug_string(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    const char *message = ((fusedwasi_p2_error *)fusedwasi_p2_table_get(s, self, FUSEDWASI_P2_KIND_ERROR)->obj)->message;
    uint32_t n = (uint32_t)strlen(message);
    uint32_t ptr = fusedwasi_p2_alloc(s, comp, 1, n);
    if (n) memcpy(fusedwasi_p2_mem(s, comp, ptr, n), message, n);
    fusedwasi_p2_store_u32(s, comp, retptr, ptr);
    fusedwasi_p2_store_u32(s, comp, retptr + 4, n);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_error_error_drop(fusedwasi_p2_state *s, uint32_t handle) { fusedwasi_p2_table_drop(s, handle, FUSEDWASI_P2_KIND_ERROR); }

/* poll */

FUSEDWASI_P2_FN fusedwasi_p2_pollable *fusedwasi_p2_pollable_get(fusedwasi_p2_state *s, uint32_t handle) {
    return (fusedwasi_p2_pollable *)fusedwasi_p2_table_get(s, handle, FUSEDWASI_P2_KIND_POLLABLE)->obj;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_io_poll_pollable_ready(fusedwasi_p2_state *s, uint32_t self) {
    fusedwasi_p2_pollable *p = fusedwasi_p2_pollable_get(s, self);
    return (uint32_t)(p->ready(s, p) != 0);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_poll_pollable_block(fusedwasi_p2_state *s, uint32_t self) {
    fusedwasi_p2_pollable *p = fusedwasi_p2_pollable_get(s, self);
    while (!p->ready(s, p)) s->wait(s);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_poll_pollable_drop(fusedwasi_p2_state *s, uint32_t handle) { fusedwasi_p2_table_drop(s, handle, FUSEDWASI_P2_KIND_POLLABLE); }

/* Waits until one of the pollables is ready and gives the indexes of all that are (a list of u32). An empty list is a trap. */
FUSEDWASI_P2_FN void fusedwasi_wasi_io_poll_poll(fusedwasi_p2_state *s, uint32_t comp, uint32_t in_ptr, uint32_t in_len, uint32_t retptr) {
    if (in_len == 0) s->trap(s, "poll of an empty list");
    fusedwasi_p2_pollable **list = (fusedwasi_p2_pollable **)malloc(in_len * sizeof *list);
    uint32_t *ready = (uint32_t *)malloc(in_len * sizeof *ready);
    if (!list || !ready) s->trap(s, "out of memory");
    for (uint32_t i = 0; i < in_len; i++) list[i] = fusedwasi_p2_pollable_get(s, fusedwasi_p2_load_u32(s, comp, in_ptr + 4 * i));
    uint32_t count = 0;
    for (;;) {
        for (uint32_t i = 0; i < in_len; i++)
            if (list[i]->ready(s, list[i])) ready[count++] = i;
        if (count) break;
        s->wait(s);
    }
    uint32_t ptr = fusedwasi_p2_alloc(s, comp, 4, 4 * count);
    for (uint32_t k = 0; k < count; k++) fusedwasi_p2_store_u32(s, comp, ptr + 4 * k, ready[k]);
    free(list);
    free(ready);
    fusedwasi_p2_store_u32(s, comp, retptr, ptr);
    fusedwasi_p2_store_u32(s, comp, retptr + 4, count);
}

/* streams */

FUSEDWASI_P2_FN fusedwasi_p2_stream *fusedwasi_p2_input_get(fusedwasi_p2_state *s, uint32_t handle) {
    return (fusedwasi_p2_stream *)fusedwasi_p2_table_get(s, handle, FUSEDWASI_P2_KIND_INPUT_STREAM)->obj;
}

FUSEDWASI_P2_FN fusedwasi_p2_stream *fusedwasi_p2_output_get(fusedwasi_p2_state *s, uint32_t handle) {
    return (fusedwasi_p2_stream *)fusedwasi_p2_table_get(s, handle, FUSEDWASI_P2_KIND_OUTPUT_STREAM)->obj;
}

/* Up to `len` bytes into a buffer from malloc (`*out`); when `block`, waits for at least one. Returns the status; the buffer is the caller's to free. */
FUSEDWASI_P2_FN int fusedwasi_p2_read(fusedwasi_p2_state *s, fusedwasi_p2_stream *st, uint64_t len, int block, uint8_t **out, uint64_t *got) {
    uint64_t n = len < FUSEDWASI_P2_READ_CAP ? len : FUSEDWASI_P2_READ_CAP;
    uint8_t *buf = (uint8_t *)malloc(n ? (size_t)n : 1);
    if (!buf) s->trap(s, "out of memory");
    for (;;) {
        *got = 0;
        int status = fusedwasi_p2_s_read(s, st, buf, n, got);
        if (status != FUSEDWASI_P2_STREAM_OK || *got || !block || n == 0) {
            *out = buf;
            return status;
        }
        s->wait(s);
    }
}

FUSEDWASI_P2_FN void fusedwasi_p2_read_list(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t len, uint32_t retptr, int block) {
    fusedwasi_p2_stream *st = fusedwasi_p2_input_get(s, self);
    uint8_t *buf;
    uint64_t got;
    int status = fusedwasi_p2_read(s, st, len, block, &buf, &got);
    if (status != FUSEDWASI_P2_STREAM_OK) {
        free(buf);
        fusedwasi_p2_stream_err(s, comp, retptr, retptr + 4, status, st);
        return;
    }
    uint32_t ptr = fusedwasi_p2_alloc(s, comp, 1, (uint32_t)got);
    if (got) memcpy(fusedwasi_p2_mem(s, comp, ptr, (uint32_t)got), buf, (size_t)got);
    free(buf);
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_store_u32(s, comp, retptr + 4, ptr);
    fusedwasi_p2_store_u32(s, comp, retptr + 8, (uint32_t)got);
}

FUSEDWASI_P2_FN void fusedwasi_p2_skip(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t len, uint32_t retptr, int block) {
    fusedwasi_p2_stream *st = fusedwasi_p2_input_get(s, self);
    uint8_t *buf;
    uint64_t got;
    int status = fusedwasi_p2_read(s, st, len, block, &buf, &got);
    free(buf);
    if (status != FUSEDWASI_P2_STREAM_OK) {
        fusedwasi_p2_stream_err(s, comp, retptr, retptr + 8, status, st);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_store_u64(s, comp, retptr + 8, got);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_input_stream_read(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t len, uint32_t retptr) {
    fusedwasi_p2_read_list(s, comp, self, len, retptr, 0);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_input_stream_blocking_read(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t len, uint32_t retptr) {
    fusedwasi_p2_read_list(s, comp, self, len, retptr, 1);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_input_stream_skip(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t len, uint32_t retptr) {
    fusedwasi_p2_skip(s, comp, self, len, retptr, 0);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_input_stream_blocking_skip(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t len, uint32_t retptr) {
    fusedwasi_p2_skip(s, comp, self, len, retptr, 1);
}

FUSEDWASI_P2_FN int fusedwasi_p2_stream_pollable_ready(fusedwasi_p2_state *s, fusedwasi_p2_pollable *p) {
    fusedwasi_p2_stream *st = (fusedwasi_p2_stream *)p->ctx;
    return st->dead || !st->ready || st->ready(s, st); /* a closed stream is always ready */
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_stream_pollable(fusedwasi_p2_state *s, fusedwasi_p2_stream *st) {
    fusedwasi_p2_pollable *p = (fusedwasi_p2_pollable *)calloc(1, sizeof *p);
    if (!p) s->trap(s, "out of memory");
    p->ready = fusedwasi_p2_stream_pollable_ready;
    p->ctx = st;
    return fusedwasi_p2_table_new(s, FUSEDWASI_P2_KIND_POLLABLE, p, fusedwasi_p2_free_obj);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_io_streams_input_stream_subscribe(fusedwasi_p2_state *s, uint32_t self) {
    return fusedwasi_p2_stream_pollable(s, fusedwasi_p2_input_get(s, self));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_input_stream_drop(fusedwasi_p2_state *s, uint32_t handle) {
    fusedwasi_p2_input_get(s, handle); /* a bad handle traps before anything is dropped */
    fusedwasi_p2_table_drop(s, handle, FUSEDWASI_P2_KIND_INPUT_STREAM);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_output_stream_check_write(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_stream *st = fusedwasi_p2_output_get(s, self);
    int status = FUSEDWASI_P2_STREAM_OK;
    uint64_t budget = fusedwasi_p2_s_check_write(s, st, &status);
    if (status != FUSEDWASI_P2_STREAM_OK) {
        fusedwasi_p2_stream_err(s, comp, retptr, retptr + 8, status, st);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_store_u64(s, comp, retptr + 8, budget);
}

/* One write, limited by the budget (a bigger one traps); returns 0 and writes the error if it failed. */
FUSEDWASI_P2_FN int fusedwasi_p2_write(fusedwasi_p2_state *s, uint32_t comp, fusedwasi_p2_stream *st, const uint8_t *data, uint64_t len, uint32_t retptr) {
    int status = FUSEDWASI_P2_STREAM_OK;
    uint64_t budget = fusedwasi_p2_s_check_write(s, st, &status);
    if (status == FUSEDWASI_P2_STREAM_OK) {
        if (len > budget) s->trap(s, "write of more than check-write allows");
        status = fusedwasi_p2_s_write(s, st, data, len);
    }
    if (status != FUSEDWASI_P2_STREAM_OK) {
        fusedwasi_p2_stream_err(s, comp, retptr, retptr + 4, status, st);
        return 0;
    }
    return 1;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_output_stream_write(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t len, uint32_t retptr) {
    fusedwasi_p2_stream *st = fusedwasi_p2_output_get(s, self);
    const uint8_t *data = fusedwasi_p2_mem(s, comp, ptr, len);
    if (fusedwasi_p2_write(s, comp, st, data, len, retptr)) fusedwasi_p2_ok(s, comp, retptr);
}

/* Writes it all in pieces as the budget allows, waiting when it is 0, then flushes. `data` NULL means zeroes. */
FUSEDWASI_P2_FN void fusedwasi_p2_write_all_and_flush(fusedwasi_p2_state *s, uint32_t comp, fusedwasi_p2_stream *st, const uint8_t *data, uint64_t len, uint32_t retptr) {
    static const uint8_t zeroes[4096] = {0};
    int status = FUSEDWASI_P2_STREAM_OK;
    while (len) {
        uint64_t budget = fusedwasi_p2_s_check_write(s, st, &status);
        if (status != FUSEDWASI_P2_STREAM_OK) break;
        if (budget == 0) {
            s->wait(s);
            continue;
        }
        uint64_t n = len < budget ? len : budget;
        if (!data && n > sizeof zeroes) n = sizeof zeroes;
        status = fusedwasi_p2_s_write(s, st, data ? data : zeroes, n);
        if (status != FUSEDWASI_P2_STREAM_OK) break;
        if (data) data += n;
        len -= n;
    }
    if (status == FUSEDWASI_P2_STREAM_OK) status = fusedwasi_p2_s_flush(s, st);
    if (status != FUSEDWASI_P2_STREAM_OK) {
        fusedwasi_p2_stream_err(s, comp, retptr, retptr + 4, status, st);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_output_stream_blocking_write_and_flush(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t len,
                                                                       uint32_t retptr) {
    fusedwasi_p2_stream *st = fusedwasi_p2_output_get(s, self);
    fusedwasi_p2_write_all_and_flush(s, comp, st, fusedwasi_p2_mem(s, comp, ptr, len), len, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_output_stream_write_zeroes(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t len, uint32_t retptr) {
    fusedwasi_p2_stream *st = fusedwasi_p2_output_get(s, self);
    uint8_t *zeroes = (uint8_t *)calloc(len ? (size_t)len : 1, 1);
    if (!zeroes) s->trap(s, "out of memory");
    int ok = fusedwasi_p2_write(s, comp, st, zeroes, len, retptr);
    free(zeroes);
    if (ok) fusedwasi_p2_ok(s, comp, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_output_stream_blocking_write_zeroes_and_flush(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t len,
                                                                              uint32_t retptr) {
    fusedwasi_p2_write_all_and_flush(s, comp, fusedwasi_p2_output_get(s, self), NULL, len, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_output_stream_flush(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_stream *st = fusedwasi_p2_output_get(s, self);
    int status = fusedwasi_p2_s_flush(s, st);
    if (status != FUSEDWASI_P2_STREAM_OK) {
        fusedwasi_p2_stream_err(s, comp, retptr, retptr + 4, status, st);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_output_stream_blocking_flush(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_wasi_io_streams_output_stream_flush(s, comp, self, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_p2_splice(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t src, uint64_t len, uint32_t retptr, int block) {
    fusedwasi_p2_stream *out = fusedwasi_p2_output_get(s, self);
    fusedwasi_p2_stream *in = fusedwasi_p2_input_get(s, src);
    uint8_t *buf = NULL;
    uint64_t got = 0;
    int status = FUSEDWASI_P2_STREAM_OK;
    fusedwasi_p2_stream *failed = NULL; /* the stream that failed, for the error */
    for (;;) {
        uint64_t budget = fusedwasi_p2_s_check_write(s, out, &status);
        if (status != FUSEDWASI_P2_STREAM_OK) {
            failed = out;
            break;
        }
        if (budget == 0 && block) {
            s->wait(s);
            continue;
        }
        if (budget) {
            status = fusedwasi_p2_read(s, in, len < budget ? len : budget, block, &buf, &got);
            if (status != FUSEDWASI_P2_STREAM_OK) {
                failed = in;
                break;
            }
            status = fusedwasi_p2_s_write(s, out, buf, got);
            if (status != FUSEDWASI_P2_STREAM_OK) failed = out;
        }
        break;
    }
    free(buf);
    if (status != FUSEDWASI_P2_STREAM_OK) {
        fusedwasi_p2_stream_err(s, comp, retptr, retptr + 8, status, failed);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_store_u64(s, comp, retptr + 8, got);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_output_stream_splice(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t src, uint64_t len, uint32_t retptr) {
    fusedwasi_p2_splice(s, comp, self, src, len, retptr, 0);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_output_stream_blocking_splice(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t src, uint64_t len, uint32_t retptr) {
    fusedwasi_p2_splice(s, comp, self, src, len, retptr, 1);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_io_streams_output_stream_subscribe(fusedwasi_p2_state *s, uint32_t self) {
    return fusedwasi_p2_stream_pollable(s, fusedwasi_p2_output_get(s, self));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_io_streams_output_stream_drop(fusedwasi_p2_state *s, uint32_t handle) {
    fusedwasi_p2_output_get(s, handle);
    fusedwasi_p2_table_drop(s, handle, FUSEDWASI_P2_KIND_OUTPUT_STREAM);
}

#endif /* FUSEDWASI_P2_IO_H */
