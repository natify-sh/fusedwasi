/* wasi:cli (13 imports): environment, exit, stdin, stdout, stderr and the terminals (the twin of p2/wasi_p2_cli.py, where the layouts are described).
 *
 * environment: `state->args`, `state->env_names`/`env_values`, `state->initial_cwd` (owned by the caller). exit: `g->exit` is told the code; by default it records `exited` and `exit_code` of the guest
 * and traps. The standard streams: `state->std[i]` (a fusedwasi_p2_stream) or, by default, the descriptors 0, 1 and 2 of the process, made when first asked for; a handle to a
 * standard stream is a new handle every time, to the same stream, and dropping it closes nothing. Terminals: `state->terminals` says which are, the resources have no methods. */
#ifndef FUSEDWASI_P2_CLI_H
#define FUSEDWASI_P2_CLI_H

#include "wasi_p2_io.h"
#ifdef _WIN32
#include <io.h>
#else
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#endif

#define FUSEDWASI_P2_STD_CHUNK 4096u

/* a stream over a descriptor of the process */
typedef struct {
    fusedwasi_p2_stream base;
    int fd;
} fusedwasi_p2_fd_stream;

FUSEDWASI_P2_FN int fusedwasi_p2_fd_ready_in(fusedwasi_p2_state *state, fusedwasi_p2_stream *st) {
    (void)state;
#ifdef _WIN32
    (void)st;
    return 1;
#else
    struct pollfd p = {((fusedwasi_p2_fd_stream *)st)->fd, POLLIN, 0};
    return poll(&p, 1, 0) > 0;
#endif
}

FUSEDWASI_P2_FN int fusedwasi_p2_fd_read(fusedwasi_p2_state *state, fusedwasi_p2_stream *st, uint8_t *buf, uint64_t len, uint64_t *got) {
    fusedwasi_p2_fd_stream *f = (fusedwasi_p2_fd_stream *)st;
    *got = 0;
    if (len == 0 || !fusedwasi_p2_fd_ready_in(state, st)) return FUSEDWASI_P2_STREAM_OK;
#ifdef _WIN32
    int n = _read(f->fd, buf, (unsigned)len);
#else
    fusedwasi_p2_ssize n;
    do n = read(f->fd, buf, (size_t)len);
    while (n < 0 && errno == EINTR);
#endif
    if (n < 0) {
        st->failure = "read failed";
        st->failure_errno = errno;
        return FUSEDWASI_P2_STREAM_FAILED;
    }
    if (n == 0) return FUSEDWASI_P2_STREAM_CLOSED;
    *got = (uint64_t)n;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN uint64_t fusedwasi_p2_fd_check_write(fusedwasi_p2_state *state, fusedwasi_p2_stream *st, int *status) {
    (void)state, (void)st;
    *status = FUSEDWASI_P2_STREAM_OK;
    return FUSEDWASI_P2_STD_CHUNK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_fd_write(fusedwasi_p2_state *state, fusedwasi_p2_stream *st, const uint8_t *data, uint64_t len) {
    fusedwasi_p2_fd_stream *f = (fusedwasi_p2_fd_stream *)st;
    (void)state;
    while (len) {
#ifdef _WIN32
        int n = _write(f->fd, data, (unsigned)len);
#else
        fusedwasi_p2_ssize n = write(f->fd, data, (size_t)len);
        if (n < 0 && errno == EINTR) continue;
#endif
        if (n <= 0) {
            st->failure = "write failed";
            st->failure_errno = n < 0 ? errno : EIO;
            return FUSEDWASI_P2_STREAM_FAILED;
        }
        data += n;
        len -= (uint64_t)n;
    }
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_fd_flush(fusedwasi_p2_state *state, fusedwasi_p2_stream *st) {
    (void)state, (void)st;
    return FUSEDWASI_P2_STREAM_OK;
}

/* Streams on descriptors, owned by the caller (`free` them after the state): for the standard streams of a state, or for tests. */
/* A stream over a descriptor of the process, to give to `std[i]` or `fusedwasi_p2_input_stream_add`; NULL when there is no memory. No guest is needed. Free it after the state. */
FUSEDWASI_P2_FN fusedwasi_p2_fd_stream *fusedwasi_p2_fd_input_new(fusedwasi_p2_state *state, int fd) {
    fusedwasi_p2_fd_stream *f = (fusedwasi_p2_fd_stream *)calloc(1, sizeof *f);
    (void)state;
    if (!f) return NULL;
    f->fd = fd;
    f->base.read = fusedwasi_p2_fd_read;
    f->base.ready = fusedwasi_p2_fd_ready_in;
    return f;
}

FUSEDWASI_P2_FN fusedwasi_p2_fd_stream *fusedwasi_p2_fd_output_new(fusedwasi_p2_state *state, int fd) {
    fusedwasi_p2_fd_stream *f = (fusedwasi_p2_fd_stream *)calloc(1, sizeof *f);
    (void)state;
    if (!f) return NULL;
    f->fd = fd;
    f->base.check_write = fusedwasi_p2_fd_check_write;
    f->base.write = fusedwasi_p2_fd_write;
    f->base.flush = fusedwasi_p2_fd_flush;
    f->base.ready = fusedwasi_p2_always_ready;
    return f;
}

/* the standard stream i (0 stdin, 1 stdout, 2 stderr); NULL when the default one cannot be made (no memory) */
FUSEDWASI_P2_FN fusedwasi_p2_stream *fusedwasi_p2_std_get(fusedwasi_p2_state *state, int i) {
    if (state->std[i]) return state->std[i];
    if (!state->std_default[i]) state->std_default[i] = i == 0 ? (void *)fusedwasi_p2_fd_input_new(state, 0) : (void *)fusedwasi_p2_fd_output_new(state, i);
    return (fusedwasi_p2_stream *)state->std_default[i];
}

/* the same in a function that has the guest: no memory for the default stream is a trap */
FUSEDWASI_P2_FN fusedwasi_p2_stream *fusedwasi_p2_std(fusedwasi_p2_guest *g, int i) {
    fusedwasi_p2_stream *st = fusedwasi_p2_std_get(g->state, i);
    if (!st) g->trap(g, "out of memory");
    return st;
}

/* environment */

FUSEDWASI_P2_FN void fusedwasi_wasi_cli_environment_get_environment(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr) {
    uint32_t ptr = fusedwasi_p2_alloc(g, comp, 4, 16 * g->state->env_count);
    for (uint32_t i = 0; i < g->state->env_count; i++) {
        fusedwasi_p2_string(g, comp, g->state->env_names[i], ptr + 16 * i);
        fusedwasi_p2_string(g, comp, g->state->env_values[i], ptr + 16 * i + 8);
    }
    fusedwasi_p2_store_u32(g, comp, retptr, ptr);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, g->state->env_count);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_cli_environment_get_arguments(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr) {
    uint32_t ptr = fusedwasi_p2_alloc(g, comp, 4, 8 * g->state->arg_count);
    for (uint32_t i = 0; i < g->state->arg_count; i++) fusedwasi_p2_string(g, comp, g->state->args[i], ptr + 8 * i);
    fusedwasi_p2_store_u32(g, comp, retptr, ptr);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, g->state->arg_count);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_cli_environment_initial_cwd(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr) {
    if (!g->state->initial_cwd) {
        fusedwasi_p2_store_u8(g, comp, retptr, 0);
        return;
    }
    fusedwasi_p2_string(g, comp, g->state->initial_cwd, retptr + 4);
    fusedwasi_p2_store_u8(g, comp, retptr, 1);
}

/* exit */

FUSEDWASI_P2_FN void fusedwasi_wasi_cli_exit_exit(fusedwasi_p2_guest *g, uint32_t status) { g->exit(g, status == 0 ? 0 : 1); }

FUSEDWASI_P2_FN void fusedwasi_wasi_cli_exit_exit_with_code(fusedwasi_p2_guest *g, uint32_t status_code) { g->exit(g, status_code & 0xFF); }

/* the standard streams: a handle that does not close the stream when dropped */

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_cli_stdin_get_stdin(fusedwasi_p2_guest *g) {
    return fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_INPUT_STREAM, fusedwasi_p2_std(g, 0), NULL);
}
FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_cli_stdout_get_stdout(fusedwasi_p2_guest *g) {
    return fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_OUTPUT_STREAM, fusedwasi_p2_std(g, 1), NULL);
}
FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_cli_stderr_get_stderr(fusedwasi_p2_guest *g) {
    return fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_OUTPUT_STREAM, fusedwasi_p2_std(g, 2), NULL);
}

/* the terminals */

FUSEDWASI_P2_FN void fusedwasi_p2_terminal(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, unsigned bit, uint32_t kind) {
    if (g->state->terminals & bit) {
        fusedwasi_p2_store_u8(g, comp, retptr, 1);
        fusedwasi_p2_store_u32(g, comp, retptr + 4, fusedwasi_p2_table_new(g, kind, NULL, NULL));
    } else {
        fusedwasi_p2_store_u8(g, comp, retptr, 0);
    }
}

FUSEDWASI_P2_FN void fusedwasi_wasi_cli_terminal_stdin_get_terminal_stdin(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr) {
    fusedwasi_p2_terminal(g, comp, retptr, FUSEDWASI_P2_TERMINAL_STDIN, FUSEDWASI_P2_KIND_TERMINAL_INPUT);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_cli_terminal_stdout_get_terminal_stdout(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr) {
    fusedwasi_p2_terminal(g, comp, retptr, FUSEDWASI_P2_TERMINAL_STDOUT, FUSEDWASI_P2_KIND_TERMINAL_OUTPUT);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_cli_terminal_stderr_get_terminal_stderr(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr) {
    fusedwasi_p2_terminal(g, comp, retptr, FUSEDWASI_P2_TERMINAL_STDERR, FUSEDWASI_P2_KIND_TERMINAL_OUTPUT);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_cli_terminal_input_terminal_input_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_TERMINAL_INPUT);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_cli_terminal_output_terminal_output_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2_table_drop(g, handle, FUSEDWASI_P2_KIND_TERMINAL_OUTPUT);
}

#endif /* FUSEDWASI_P2_CLI_H */
