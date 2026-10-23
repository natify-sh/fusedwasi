/* Runs a script of WASI 0.1 calls (the files of tests/data/p1) on the C core, with a fake memory, and prints what happens. test_wasi_p1_core.py does the same in Python and compares. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasi_p1_core.h"
#include "wasi_p1.h"

#define MAX_TOKENS 16
static uint8_t memory[1 << 16];
static uint8_t *memory_base = memory;
static uint64_t memory_size = 0;

static struct {
    char stdin_data[4096];
    size_t stdin_len, stdin_pos;
    int stdout_status, clock_status, random_status;
} host;

static int hook_stdin(void *user, uint8_t *buf, size_t cap, size_t *got) {
    (void)user;
    size_t left = host.stdin_len - host.stdin_pos, n = left < cap ? left : cap;
    memcpy(buf, host.stdin_data + host.stdin_pos, n);
    host.stdin_pos += n;
    *got = n;
    return 0;
}

static void hex(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
}

static int hook_stdout(void *user, int fd, const uint8_t *buf, size_t n) {
    (void)user;
    if (host.stdout_status) return host.stdout_status;
    printf("out %d ", fd);
    hex(buf, n);
    printf("\n");
    return 0;
}

static int hook_clock(void *user, uint32_t id, uint64_t *ns) {
    (void)user;
    if (host.clock_status) return host.clock_status;
    *ns = 1000 + 7 * (uint64_t)id;
    return 0;
}

static int hook_random(void *user, uint8_t *buf, size_t n) {
    (void)user;
    if (host.random_status) return host.random_status;
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)((i * 3 + 1) & 0xff);
    return 0;
}

static void hook_end(void *user, int how, uint32_t value) {
    (void)user;
    printf("end %d %u\n", how, (unsigned)value);
}

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

typedef uint32_t (*call_fn)(fusedwasi_p1_state *w, const uint64_t *a, int *trapped);
#define A(i) a[i]
#define CALL(name, n, ...) \
    static uint32_t c_##name(fusedwasi_p1_state *w, const uint64_t *a, int *t) { (void)a; return fusedwasi_p1_##name(w, __VA_ARGS__ t); }
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
CALL(fd_seek, 4, (uint32_t)A(0), A(1), (uint32_t)A(2), (uint32_t)A(3), )
CALL(fd_tell, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(fd_close, 1, (uint32_t)A(0), )
CALL(fd_renumber, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(fd_advise, 4, (uint32_t)A(0), A(1), A(2), (uint32_t)A(3), )
CALL(fd_fdstat_get, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(fd_prestat_get, 2, (uint32_t)A(0), (uint32_t)A(1), )
CALL(fd_prestat_dir_name, 3, (uint32_t)A(0), (uint32_t)A(1), (uint32_t)A(2), )

static uint32_t c_proc_exit(fusedwasi_p1_state *w, const uint64_t *a, int *t) {
    fusedwasi_p1_proc_exit(w, (uint32_t)a[0], t);
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
};

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const char *tmp = argv[1];
    FILE *script = fopen(argv[2], "r");
    if (script == NULL) return 2;
    char args[1024] = "", env[1024] = "";
    size_t args_size = 0, env_size = 0;
    uint32_t argc_wasi = 0, envc = 0;
    fusedwasi_p1_preopen preopens[4];
    char preopen_host[4][512], preopen_name[4][64];
    size_t npreopens = 0;
    int hooks_on = 0;
    fusedwasi_p1_state *w = NULL;
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
            args_size += l[1] + 1;
            argc_wasi++;
        } else if (!strcmp(t[0], "env")) {
            memcpy(env + env_size, t[1], l[1] + 1);
            env_size += l[1] + 1;
            envc++;
        } else if (!strcmp(t[0], "preopen")) {
            snprintf(preopen_host[npreopens], sizeof preopen_host[0], "%s", !strcmp(t[1], "$TMP") ? tmp : t[1]);
            snprintf(preopen_name[npreopens], sizeof preopen_name[0], "%s", t[2]);
            preopens[npreopens].name = preopen_name[npreopens];
            preopens[npreopens].name_len = l[2];
            preopens[npreopens].host = preopen_host[npreopens];
            preopens[npreopens].readonly = n > 3 && !strcmp(t[3], "ro");
            npreopens++;
        } else if (!strcmp(t[0], "hooks")) {
            hooks_on = !strcmp(t[1], "on");
        } else if (!strcmp(t[0], "start")) {
            fusedwasi_p1_config cfg;
            memset(&cfg, 0, sizeof cfg);
            cfg.args = argc_wasi ? args : NULL;
            cfg.args_size = args_size;
            cfg.argc = argc_wasi;
            cfg.program = "program";
            cfg.env = env;
            cfg.env_size = env_size;
            cfg.envc = envc;
            cfg.preopens = preopens;
            cfg.npreopens = npreopens;
            if (hooks_on) {
                cfg.hooks.stdin_read = hook_stdin;
                cfg.hooks.stdout_write = hook_stdout;
                cfg.hooks.clock = hook_clock;
                cfg.hooks.random = hook_random;
                cfg.hooks.end = hook_end;
            }
            int err = 0;
            size_t bad = 0;
            w = fusedwasi_p1_create(&cfg, &err, &bad);
            if (w == NULL) {
                printf("create failed %d %d\n", err, (int)bad);
                return 1;
            }
            memory_size = (uint64_t)atoi(t[1]);
            fusedwasi_p1_bind(w, &memory_base, &memory_size);
        } else if (!strcmp(t[0], "stdin")) {
            host.stdin_len = l[1];
            host.stdin_pos = 0;
            memcpy(host.stdin_data, t[1], l[1]);
        } else if (!strcmp(t[0], "stdout_status")) {
            host.stdout_status = atoi(t[1]);
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
            } else if (!strcmp(t[1], "dump")) {
                printf("%s ", t[4]);
                hex(memory + strtoull(t[2], NULL, 0), (size_t)strtoull(t[3], NULL, 0));
                printf("\n");
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
            uint32_t rc = calls[i].fn(w, a, &trapped);
            if (trapped) printf("%s trapped\n", t[1]);
            else printf("%s %u\n", t[1], (unsigned)rc);
        }
    }
    fusedwasi_p1_destroy(w);
    return 0;
}
