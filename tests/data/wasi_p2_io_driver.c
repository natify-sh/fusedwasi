/* The scenario of tests/test_wasi_p2_io.py, in C: a fake memory, a bump allocator for cabi_realloc, the output is compared with the one of the Python host. */
#include <inttypes.h>
#include "wasi_p2_io.h"

static void trap(fusedwasi_p2_guest *s, const char *m) { (void)s; printf("TRAP %s\n", m); exit(0); }

static uint32_t bump(fusedwasi_p2_guest *s, uint32_t comp, uint32_t old_ptr, uint32_t old_size, uint32_t align, uint32_t new_size) {
    uint32_t *next = (uint32_t *)s->user;
    (void)comp, (void)old_ptr, (void)old_size;
    uint32_t ptr = (*next + align - 1) / align * align;
    *next = ptr + new_size;
    return ptr;
}

static uint8_t memory[256];

/* streams that fail: the first call is an error with a message, and then the stream is closed (the rule of stream-error) */
static int failing_read(fusedwasi_p2_state *st, fusedwasi_p2_stream *stream, uint8_t *buf, uint64_t len, uint64_t *got) {
    (void)st, (void)buf, (void)len;
    *got = 0;
    stream->failure = "boom";
    return FUSEDWASI_P2_STREAM_FAILED;
}
static uint64_t failing_check_write(fusedwasi_p2_state *st, fusedwasi_p2_stream *stream, int *status) {
    (void)st;
    stream->failure = "boom";
    *status = FUSEDWASI_P2_STREAM_FAILED;
    return 0;
}
static int never_ready(fusedwasi_p2_state *st, fusedwasi_p2_stream *stream) { (void)st, (void)stream; return 0; }

static void dump(const char *what) {
    printf("%s ", what);
    for (int i = 0; i < 256; i++) printf("%02x", memory[i]);
    printf("\n");
}

static uint32_t u32_at(uint32_t off) {
    return (uint32_t)memory[off] | (uint32_t)memory[off + 1] << 8 | (uint32_t)memory[off + 2] << 16 | (uint32_t)memory[off + 3] << 24;
}

static void hex(const char *what, const uint8_t *p, uint64_t n) {
    printf("%s ", what);
    for (uint64_t i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    const char *scenario = argc > 1 ? argv[1] : "streams";
    uint32_t next = 160;
    fusedwasi_p2_state world;
    fusedwasi_p2_guest s;
    fusedwasi_p2_init(&world);
    fusedwasi_p2_guest_init(&s, &world);
    s.trap = trap;
    s.user = &next;
    s.realloc = strcmp(scenario, "no_realloc") ? bump : NULL;
    fusedwasi_p2_set_memory(&s, 0, memory, sizeof memory);
    memcpy(memory + 100, "hello", 5);
    static const uint8_t abc[] = "abcdefghij", xyz[] = "xyz";
    uint32_t i = fusedwasi_p2_memory_input_new(&world, abc, 10), j = fusedwasi_p2_memory_input_new(&world, xyz, 3), o, o2;
    fusedwasi_p2_buffer_output *bo = fusedwasi_p2_buffer_output_new(&world, 8, &o);
    fusedwasi_p2_buffer_output *bo2 = fusedwasi_p2_buffer_output_new(&world, 100, &o2);
    printf("handles %u %u %u %u\n", i, j, o, o2);
    if (!strcmp(scenario, "streams")) {
        fusedwasi_wasi_io_streams_input_stream_read(&s, 0, i, 4, 0), dump("read");
        fusedwasi_wasi_io_streams_input_stream_skip(&s, 0, i, 2, 16), dump("skip");
        fusedwasi_wasi_io_streams_output_stream_check_write(&s, 0, o, 32), dump("check_write");
        fusedwasi_wasi_io_streams_output_stream_write(&s, 0, o, 100, 5, 48), dump("write");
        fusedwasi_wasi_io_streams_output_stream_check_write(&s, 0, o, 64), dump("check_write");
        fusedwasi_wasi_io_streams_output_stream_write_zeroes(&s, 0, o, 3, 80), dump("write_zeroes");
        fusedwasi_wasi_io_streams_output_stream_check_write(&s, 0, o, 96), dump("check_write");
        fusedwasi_wasi_io_streams_output_stream_flush(&s, 0, o, 112), dump("flush");
        fusedwasi_wasi_io_streams_output_stream_splice(&s, 0, o2, j, 10, 120), dump("splice");
        fusedwasi_wasi_io_streams_output_stream_splice(&s, 0, o2, j, 10, 136), dump("splice");
        fusedwasi_wasi_io_streams_input_stream_read(&s, 0, i, 100, 0), dump("read");
        fusedwasi_wasi_io_streams_input_stream_read(&s, 0, i, 100, 16), dump("read");
        uint32_t p = fusedwasi_wasi_io_streams_input_stream_subscribe(&s, i);
        printf("pollable %u ready %u\n", p, fusedwasi_wasi_io_poll_pollable_ready(&s, p));
        fusedwasi_p2__store_u32(&s, 0, 90, p);
        fusedwasi_wasi_io_poll_poll(&s, 0, 90, 1, 32), dump("poll");
        fusedwasi_wasi_io_poll_pollable_block(&s, p);
        uint32_t e = fusedwasi_p2__error_new_codes(&s, "boom", -1, -1);
        fusedwasi_wasi_io_error_error_to_debug_string(&s, 0, e, 48), dump("to_debug_string");
        fusedwasi_wasi_io_error_error_drop(&s, e);
        fusedwasi_wasi_io_streams_output_stream_blocking_write_and_flush(&s, 0, o2, 100, 5, 64), dump("blocking_write_and_flush");
        fusedwasi_wasi_io_streams_output_stream_blocking_write_zeroes_and_flush(&s, 0, o2, 50, 64), dump("blocking_write_zeroes_and_flush");
        fusedwasi_wasi_io_streams_input_stream_blocking_read(&s, 0, j, 5, 16), dump("blocking_read");
        hex("o", bo->buf, bo->len);
        printf("o2 %" PRIu64 "\n", bo2->len);
        hex("o2", bo2->buf, 16);
        fusedwasi_wasi_io_poll_pollable_drop(&s, p);
        fusedwasi_wasi_io_streams_input_stream_drop(&s, i);
        fusedwasi_wasi_io_streams_output_stream_drop(&s, o);
    } else if (!strcmp(scenario, "failure")) {
        fusedwasi_p2_stream *fin = (fusedwasi_p2_stream *)calloc(1, sizeof *fin), *fout = (fusedwasi_p2_stream *)calloc(1, sizeof *fout);
        fin->read = failing_read, fin->ready = never_ready, fin->destroy = fusedwasi_p2__stream_free;
        fout->check_write = failing_check_write, fout->ready = never_ready, fout->destroy = fusedwasi_p2__stream_free;
        uint32_t hfi = fusedwasi_p2__input_stream_new(&s, fin), hfo = fusedwasi_p2__output_stream_new(&s, fout);
        printf("failing %u %u\n", hfi, hfo);
        fusedwasi_wasi_io_streams_input_stream_read(&s, 0, hfi, 4, 0), dump("read fails");
        fusedwasi_wasi_io_streams_input_stream_read(&s, 0, hfi, 4, 16), dump("read after the failure");
        uint32_t p = fusedwasi_wasi_io_streams_input_stream_subscribe(&s, hfi);
        printf("pollable of a closed stream: ready %u\n", fusedwasi_wasi_io_poll_pollable_ready(&s, p));
        fusedwasi_wasi_io_error_error_to_debug_string(&s, 0, u32_at(8), 80), dump("message");
        fusedwasi_wasi_io_streams_output_stream_check_write(&s, 0, hfo, 96), dump("check_write fails");
        fusedwasi_wasi_io_streams_output_stream_write(&s, 0, hfo, 100, 1, 112), dump("write after the failure");
        fusedwasi_wasi_io_streams_output_stream_flush(&s, 0, hfo, 128), dump("flush after the failure");
    } else if (!strcmp(scenario, "bad_handle")) {
        fusedwasi_wasi_io_streams_input_stream_read(&s, 0, o, 4, 0);
    } else if (!strcmp(scenario, "oversize")) {
        fusedwasi_wasi_io_streams_output_stream_write(&s, 0, o, 100, 5, 0); /* 5 fits in 8 */
        fusedwasi_wasi_io_streams_output_stream_write(&s, 0, o, 100, 5, 0);             /* 3 are left: traps */
    } else if (!strcmp(scenario, "empty_poll")) {
        fusedwasi_wasi_io_poll_poll(&s, 0, 90, 0, 32);
    } else if (!strcmp(scenario, "no_realloc")) {
        fusedwasi_wasi_io_streams_input_stream_read(&s, 0, i, 4, 0);
    }
    fusedwasi_p2_free(&world); /* the state first: it looks at the streams that are still alive */
    fusedwasi_p2_buffer_output_release(bo);
    fusedwasi_p2_buffer_output_release(bo2);
    return 0;
}
