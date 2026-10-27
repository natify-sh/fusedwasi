/* Runs a script of WASI 0.1 calls (the files of tests/data/p1) on the C core, with a fake memory, and prints what happens. test_wasi_p1_core.py does the same in Python and compares. */
#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "wasi_p1_core.h"
#include "wasi_p1.h"
#include "wasi_p1_unstable.h"

#define MAX_TOKENS 16
static uint8_t memory[1 << 16];
static uint8_t *memory_base = memory;
static uint64_t memory_size = 0;

static struct {
    char stdin_data[4096];
    size_t stdin_len, stdin_pos;
    int stdout_status, clock_status, random_status;
} host;

/* the standard streams of the state of 0.2: the input is the bytes the script gave, the outputs print what is written */
static int stream_stdin(fusedwasi_p2_state *core, fusedwasi_p2_stream *st, uint8_t *buf, uint64_t cap, uint64_t *got) {
    (void)core, (void)st;
    uint64_t left = host.stdin_len - host.stdin_pos, n = left < cap ? left : cap;
    memcpy(buf, host.stdin_data + host.stdin_pos, (size_t)n);
    host.stdin_pos += (size_t)n;
    *got = n;
    return FUSEDWASI_P2_STREAM_OK;
}

static void hex(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
}

static int stream_stdout(fusedwasi_p2_state *core, fusedwasi_p2_stream *st, const uint8_t *buf, uint64_t n) {
    (void)core;
    if (host.stdout_status) {
        st->failure_errno = host.stdout_status;
        return FUSEDWASI_P2_STREAM_FAILED;
    }
    printf("out %d ", (int)(intptr_t)st->ctx);
    hex(buf, (size_t)n);
    printf("\n");
    return FUSEDWASI_P2_STREAM_OK;
}

static int hook_clock(fusedwasi_p2_state *core, uint32_t id, uint64_t *ns) {
    (void)core;
    if (host.clock_status) return host.clock_status;
    *ns = 1000 + 7 * (uint64_t)id;
    return 0;
}

static int hook_random(fusedwasi_p2_state *core, uint8_t *buf, size_t n) {
    (void)core;
    if (host.random_status) return host.random_status;
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)((i * 3 + 1) & 0xff);
    return 0;
}

static int hook_sleep(fusedwasi_p2_state *core, uint64_t ns) {
    (void)core;
    printf("sleep %llu\n", (unsigned long long)ns);
    return 0;
}

/* a trap of the guest ends the call: the hook jumps back to `main`, which prints `trapped` */
static jmp_buf call_jump;
static void hook_trap(fusedwasi_p2_guest *core, const char *message) {
    (void)core, (void)message;
    longjmp(call_jump, 1);
}

/* the hook `signal`: tells the number; 9 ends the program by a trap of the guest, 20 is accepted, any other is refused with PERM */
static uint32_t hook_signal(fusedwasi_p1_guest *g, uint32_t number) {
    printf("signal %u\n", (unsigned)number);
    if (number == 9) g->core.trap(&g->core, "killed");
    return number == 20 ? FUSEDWASI_P1_E_SUCCESS : FUSEDWASI_P1_E_PERM;
}

/* the script gives an errno of WASI; the stream gives the errno of the system (EINVAL for 28) */
static int errno_of_wasi(int wasi) { return wasi == 28 ? EINVAL : wasi ? EIO : 0; }

static int unstable;  /* the first snapshot: the calls that differ are those of wasi_p1_unstable.h */

/* one script line in tokens: words, or "quoted strings" with \n \0 \xHH \\ \" (a token ends with a NUL, and its length is kept: it may hold NULs) */
static int tokenize(char *line, char *tokens[], size_t lens[]) {
    int n = 0;
    char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') break;
        if (n == MAX_TOKENS) return -1;
        char *out = p;
        tokens[n] = out;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\') {
                    p++;
                    if (*p == 'n') {
                        *out++ = '\n';
                    } else if (*p == '0') {
                        *out++ = '\0';
                    } else if (*p == 'x') {
                        char h[3] = {p[1], p[2], 0};
                        *out++ = (char)strtol(h, NULL, 16);
                        p += 2;
                    } else {
                        *out++ = *p;
                    }
                    p++;
                } else {
                    *out++ = *p++;
                }
            }
            if (*p == '"') p++;
            lens[n] = (size_t)(out - tokens[n]);
            *out = '\0';
        } else {
            while (*p && *p != ' ' && *p != '\t') p++;
            lens[n] = (size_t)(p - tokens[n]);
            if (*p) *p++ = '\0';
        }
        n++;
    }
    return n;
}

typedef uint32_t (*call_fn)(fusedwasi_p1_guest *g, const uint64_t *a, int *trapped);
#define A(i) a[i]
#define CALL(name, n, ...) \
    static uint32_t c_##name(fusedwasi_p1_guest *g, const uint64_t *a, int *t) { (void)a; return fusedwasi_p1_##name(g, __VA_ARGS__ t); }
CALL(args_sizes_get, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(args_get, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(environ_sizes_get, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(environ_get, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(clock_res_get, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(clock_time_get, 3, (uint32_t)A(0), A(1), (uint32_t)A(2), )
CALL(random_get, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(sched_yield, 0, )
CALL(proc_raise, 1, (uint32_t)A(0), )
CALL(fd_write, 4, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), )
CALL(fd_read, 4, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), )
static uint32_t c_fd_seek(fusedwasi_p1_guest *g, const uint64_t *a, int *t) {
    return unstable ? fusedwasi_p1_unstable_fd_seek(g, (uint32_t)A(0), A(1), (uint32_t)A(2), (uint32_t)A(3), t) : fusedwasi_p1_fd_seek(g, (uint32_t)A(0), A(1), (uint32_t)A(2), (uint32_t)A(3), t);
}
CALL(fd_tell, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(fd_close, 1, (uint32_t)A(0), )
CALL(fd_renumber, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(fd_advise, 4, (uint32_t)A(0), A(1), A(2), (uint32_t)A(3), )
static uint32_t c_fd_fdstat_get(fusedwasi_p1_guest *g, const uint64_t *a, int *t) {
    return unstable ? fusedwasi_p1_unstable_fd_fdstat_get(g, (uint32_t)A(0), (uint32_t)A(1), t) : fusedwasi_p1_fd_fdstat_get(g, (uint32_t)A(0), (uint32_t)A(1), t);
}
CALL(fd_prestat_get, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(fd_prestat_dir_name, 3, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), )

static uint32_t c_fd_filestat_get(fusedwasi_p1_guest *g, const uint64_t *a, int *t) {
    return unstable ? fusedwasi_p1_unstable_fd_filestat_get(g, (uint32_t)A(0), (uint32_t)A(1), t) : fusedwasi_p1_fd_filestat_get(g, (uint32_t)A(0), (uint32_t)A(1), t);
}
static uint32_t c_path_filestat_get(fusedwasi_p1_guest *g, const uint64_t *a, int *t) {
    return unstable ? fusedwasi_p1_unstable_path_filestat_get(g, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), (uint32_t)A(4), t) : fusedwasi_p1_path_filestat_get(g, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), (uint32_t)A(4), t);
}
CALL(fd_readdir, 5, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), A(3), (uint32_t)A(4), )
CALL(path_open, 9, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), (uint32_t)A(4), A(5), A(6), (uint32_t)A(7), (uint32_t)A(8), )
CALL(fd_datasync, 1, (uint32_t)A(0), )
CALL(fd_sync, 1, (uint32_t)A(0), )
CALL(fd_filestat_set_size, 2, (uint32_t)A(0), A(1), )
CALL(fd_allocate, 3, (uint32_t)A(0), A(1), A(2), )
CALL(fd_filestat_set_times, 4, (uint32_t)A(0), A(1), A(2), (uint32_t)A(3), )
CALL(path_filestat_set_times, 7, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), A(4), A(5), (uint32_t)A(6), )
CALL(fd_fdstat_set_flags, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(fd_fdstat_set_rights, 3, (uint32_t)A(0), A(1), A(2), )
CALL(path_create_directory, 3, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), )
CALL(path_remove_directory, 3, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), )
CALL(path_unlink_file, 3, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), )
CALL(fd_pread, 5, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), A(3), (uint32_t)A(4), )
CALL(fd_pwrite, 5, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), A(3), (uint32_t)A(4), )
CALL(path_rename, 6, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), (uint32_t)A(4), (uint32_t)A(5), )
CALL(path_link, 7, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), (uint32_t)A(4), (uint32_t)A(5), (uint32_t)A(6), )
CALL(path_symlink, 5, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), (uint32_t)A(4), )
CALL(path_readlink, 6, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), (uint32_t)A(4), (uint32_t)A(5), )

static uint32_t c_poll_oneoff(fusedwasi_p1_guest *g, const uint64_t *a, int *t) {
    return unstable ? fusedwasi_p1_unstable_poll_oneoff(g, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), t)
                    : fusedwasi_p1_poll_oneoff(g, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), t);
}
CALL(sock_accept, 3, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), )
CALL(sock_recv, 6, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), (uint32_t)A(4), (uint32_t)A(5), )
CALL(sock_send, 5, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), (uint32_t)A(3), (uint32_t)A(4), )
CALL(sock_shutdown, 2, (uint32_t)A(0), (uint32_t)A(1), )

static uint32_t c_proc_exit(fusedwasi_p1_guest *g, const uint64_t *a, int *t) {
    fusedwasi_p1_proc_exit(g, (uint32_t)a[0], t);
    return 0;
}

static const struct {
    const char *name;
    int nargs;
    call_fn fn;
} calls[] = {
#define ENTRY(name, n) {#name, n, c_##name},
    ENTRY(args_sizes_get, 2) ENTRY(args_get, 2) ENTRY(environ_sizes_get, 2) ENTRY(environ_get, 2) ENTRY(clock_res_get, 2) ENTRY(clock_time_get, 3)
    ENTRY(random_get, 2) ENTRY(sched_yield, 0) ENTRY(proc_raise, 1) ENTRY(proc_exit, 1) ENTRY(fd_write, 4) ENTRY(fd_read, 4) ENTRY(fd_seek, 4)
    ENTRY(fd_tell, 2) ENTRY(fd_close, 1) ENTRY(fd_renumber, 2) ENTRY(fd_advise, 4) ENTRY(fd_fdstat_get, 2) ENTRY(fd_prestat_get, 2)
    ENTRY(fd_prestat_dir_name, 3)
    ENTRY(fd_filestat_get, 2) ENTRY(path_filestat_get, 5) ENTRY(fd_readdir, 5) ENTRY(path_open, 9) ENTRY(fd_datasync, 1) ENTRY(fd_sync, 1)
    ENTRY(fd_filestat_set_size, 2) ENTRY(fd_allocate, 3) ENTRY(fd_filestat_set_times, 4) ENTRY(path_filestat_set_times, 7) ENTRY(fd_fdstat_set_flags, 2)
    ENTRY(fd_fdstat_set_rights, 3) ENTRY(path_create_directory, 3) ENTRY(path_remove_directory, 3) ENTRY(path_unlink_file, 3) ENTRY(fd_pread, 5)
    ENTRY(fd_pwrite, 5) ENTRY(path_rename, 6) ENTRY(path_link, 7) ENTRY(path_symlink, 5) ENTRY(path_readlink, 6)
    ENTRY(poll_oneoff, 4) ENTRY(sock_accept, 3) ENTRY(sock_recv, 6) ENTRY(sock_send, 5) ENTRY(sock_shutdown, 2)
};

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const char *tmp = argv[1];
    FILE *script = fopen(argv[2], "r");
    if (script == NULL) return 2;
    char args[1024] = "", env[1024] = "";
    size_t args_size = 0, env_size = 0;
    const char *arg_items[32], *env_names[32], *env_values[32];
    uint32_t argc_wasi = 0, envc = 0;
    fusedwasi_p2_state core;
    fusedwasi_p2_stream streams[3];
    memset(streams, 0, sizeof streams);
    fusedwasi_p2_init(&core);
    fusedwasi_p1_preopen preopens[4];
    char preopen_host[4][512], preopen_name[4][64];
    size_t npreopens = 0;
    int hooks_on = 0;
    fusedwasi_p1_state *w = NULL;
    fusedwasi_p1_guest guest;
    char line[2048];
    while (fgets(line, sizeof line, script)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        char *t[MAX_TOKENS];
        size_t l[MAX_TOKENS];
        int n = tokenize(line, t, l);
        if (n <= 0) continue;
        if (!strcmp(t[0], "arg")) {
            memcpy(args + args_size, t[1], l[1] + 1);
            arg_items[argc_wasi++] = args + args_size;
            args_size += l[1] + 1;
        } else if (!strcmp(t[0], "env")) {
            memcpy(env + env_size, t[1], l[1] + 1);
            char *eq = strchr(env + env_size, '=');
            *eq = '\0';
            env_names[envc] = env + env_size;
            env_values[envc++] = eq + 1;
            env_size += l[1] + 1;
        } else if (!strcmp(t[0], "preopen")) {
            snprintf(preopen_host[npreopens], sizeof preopen_host[0], "%s%s", strncmp(t[1], "$TMP", 4) ? "" : tmp,
                     strncmp(t[1], "$TMP", 4) ? t[1] : t[1] + 4);
            snprintf(preopen_name[npreopens], sizeof preopen_name[0], "%s", t[2]);
            preopens[npreopens].name = preopen_name[npreopens];
            preopens[npreopens].path = preopen_host[npreopens];
            preopens[npreopens].writable = !(n > 3 && !strcmp(t[3], "ro"));
            npreopens++;
        } else if (!strcmp(t[0], "mode")) {
            unstable = !strcmp(t[1], "unstable");
        } else if (!strcmp(t[0], "hooks")) {
            hooks_on = !strcmp(t[1], "on");
        } else if (!strcmp(t[0], "start")) {
            fusedwasi_p1_config cfg;
            memset(&cfg, 0, sizeof cfg);
            if (!argc_wasi) arg_items[argc_wasi++] = "program";
            core.args = arg_items;
            core.arg_count = argc_wasi;
            core.env_names = env_names;
            core.env_values = env_values;
            core.env_count = envc;
            cfg.preopens = preopens;
            cfg.npreopens = npreopens;
            if (hooks_on) {
                streams[0].read = stream_stdin;
                for (int k = 1; k < 3; k++) {
                    streams[k].write = stream_stdout;
                    streams[k].ctx = (void *)(intptr_t)k;
                }
                for (int k = 0; k < 3; k++) core.std[k] = &streams[k];
                core.clock = hook_clock;
                core.random_bytes = hook_random;
                core.sleep = hook_sleep;
            }
            int err = 0;
            size_t bad = 0;
            w = fusedwasi_p1_create(&cfg, &core, &err, &bad);
            if (w == NULL) {
                printf("create failed %d %d\n", err, (int)bad);
                return 1;
            }
            memory_size = (uint64_t)atoi(t[1]);
            fusedwasi_p1_guest_init(&guest, w);
            guest.core.trap = hook_trap;
            fusedwasi_p1_bind(&guest, &memory_base, &memory_size);
        } else if (!strcmp(t[0], "signal")) {
            guest.signal = !strcmp(t[1], "on") ? hook_signal : NULL;
        } else if (!strcmp(t[0], "stdin")) {
            host.stdin_len = l[1];
            host.stdin_pos = 0;
            memcpy(host.stdin_data, t[1], l[1]);
        } else if (!strcmp(t[0], "stdout_status")) {
            host.stdout_status = errno_of_wasi(atoi(t[1]));
        } else if (!strcmp(t[0], "clock_status")) {
            host.clock_status = atoi(t[1]);
        } else if (!strcmp(t[0], "random_status")) {
            host.random_status = atoi(t[1]);
        } else if (!strcmp(t[0], "mem")) {
            if (!strcmp(t[1], "put")) {
                memcpy(memory + strtoull(t[2], NULL, 0), t[3], l[3]);
            } else if (!strcmp(t[1], "u32")) {
                uint32_t v = (uint32_t)strtoull(t[3], NULL, 0);
                memcpy(memory + strtoull(t[2], NULL, 0), &v, 4);  /* (little-endian hosts) */
            } else if (!strcmp(t[1], "u64")) {
                uint64_t v = strtoull(t[3], NULL, 0);
                memcpy(memory + strtoull(t[2], NULL, 0), &v, 8);
            } else if (!strcmp(t[1], "zero")) {
                memset(memory + strtoull(t[2], NULL, 0), 0, (size_t)strtoull(t[3], NULL, 0));
            } else if (!strcmp(t[1], "dump")) {
                printf("%s ", t[4]);
                hex(memory + strtoull(t[2], NULL, 0), (size_t)strtoull(t[3], NULL, 0));
                printf("\n");
            }
        } else if (!strcmp(t[0], "dirents")) {  /* dirents <buffer> <used address> <label>: the entries of a readdir buffer, without the inode numbers */
            uint32_t used;
            memcpy(&used, memory + strtoull(t[2], NULL, 0), 4);
            size_t at = (size_t)strtoull(t[1], NULL, 0), end = at + used;
            while (at + 24 <= end) {
                uint64_t next;
                uint32_t namelen;
                memcpy(&next, memory + at, 8);
                memcpy(&namelen, memory + at + 16, 4);
                uint8_t type = memory[at + 20];
                size_t room = end - (at + 24), have = namelen < room ? namelen : room;
                printf("%s next %llu type %u name ", t[3], (unsigned long long)next, (unsigned)type);
                hex(memory + at + 24, have);
                printf("%s\n", have < namelen ? " (cut)" : "");
                at += 24 + have;
            }
        } else if (!strcmp(t[0], "call")) {
            size_t i = 0;
            while (i < sizeof calls / sizeof *calls && strcmp(calls[i].name, t[1])) i++;
            if (i == sizeof calls / sizeof *calls) {
                printf("unknown call %s\n", t[1]);
                return 2;
            }
            uint64_t a[MAX_TOKENS] = {0};
            for (int k = 0; k < calls[i].nargs; k++) a[k] = strtoull(t[2 + k], NULL, 0);
            int trapped = 0;
            uint32_t rc = 0;
            if (setjmp(call_jump) == 0) rc = calls[i].fn(&guest, a, &trapped);
            else trapped = 1;
            if (trapped) printf("%s trapped\nexit %d %u\n", t[1], guest.core.exited, (unsigned)guest.core.exit_code);
            else printf("%s %u\n", t[1], (unsigned)rc);
        }
    }
    fusedwasi_p1_destroy(w);
    return 0;
}
