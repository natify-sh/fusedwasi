#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS /* strtok and fopen as they are */
#define _CRT_NONSTDC_NO_WARNINGS /* strdup as it is */
#endif
/* The C host of fusedwasi (include/fusedwasi/p2/wasi_p2_filesystem.h) driven by script_fs.txt: a port of ours.py, printing the same lines.
 *
 *   cc -I src/fusedwasi/include/fusedwasi/p2 -o ours_c tools/wasi_p2_conformance/ours_c.c
 *   ours_c script_fs.txt /path/to/rw/world /path/to/ro/world
 *
 * runner.py --c builds and runs it (into a temp directory). Not part of the tests.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasi_p2_filesystem.h"

#define R 256
#define MEMSIZE (1 << 20)

static const char *KINDS[] = {"unknown", "block-device", "character-device", "directory", "fifo", "symbolic-link", "regular-file", "socket"};
static const char *ERRS[] = {
    "access", "would-block", "already", "bad-descriptor", "busy", "deadlock", "quota", "exist", "file-too-large", "illegal-byte-sequence", "in-progress",
    "interrupted", "invalid", "io", "is-directory", "loop", "too-many-links", "message-size", "name-too-long", "no-device", "no-entry", "no-lock",
    "insufficient-memory", "insufficient-space", "not-directory", "not-empty", "not-recoverable", "unsupported", "no-tty", "no-such-device", "overflow",
    "not-permitted", "pipe", "read-only", "invalid-seek", "text-file-busy", "cross-device",
};
#define FLAG_LETTERS "rwfdqm"

static fusedwasi_p2_state S;
static fusedwasi_p2_guest G;
static uint8_t *mem;
static uint32_t free_at = 0x80000, inp = 0x1000;

#define FS(fn, ...) fusedwasi_wasi_filesystem_types_##fn(&G, 0, __VA_ARGS__)
#define IO(fn, ...) fusedwasi_wasi_io_streams_##fn(&G, 0, __VA_ARGS__)

static uint32_t bump(fusedwasi_p2_guest *st, uint32_t comp, uint32_t old_ptr, uint32_t old_size, uint32_t align, uint32_t new_size) {
    (void)st, (void)comp, (void)old_ptr, (void)old_size;
    uint32_t ptr = (free_at + align - 1) / align * align;
    free_at = ptr + new_size;
    return ptr;
}
static void trap(fusedwasi_p2_guest *st, const char *m) {
    (void)st;
    fprintf(stderr, "TRAP %s\n", m);
    exit(2);
}

static uint32_t u32(uint32_t off) { uint32_t v; memcpy(&v, mem + off, 4); return v; }
static uint64_t u64(uint32_t off) { uint64_t v; memcpy(&v, mem + off, 8); return v; }
static void clear(void) { memset(mem + R, 0, 128); }
static int failed(void) { return mem[R] == 1; }

static char out[1 << 16];
static const char *err(uint32_t at) { snprintf(out, sizeof out, "err=%s", ERRS[mem[R + at]]); return out; }

static char *textof(const char *tok) {
    char *r = malloc(strlen(tok) + 1), *w = r;
    if (!strcmp(tok, "''")) { *w = 0; return r; }
    for (const char *p = tok; *p; p++) {
        if (p[0] == '\\' && p[1] == 'n') *w++ = '\n', p++;
        else *w++ = *p;
    }
    *w = 0;
    return r;
}
static void put(const char *tok, uint32_t *ptr, uint32_t *n) {
    char *t = textof(tok);
    size_t len = strlen(t);
    *ptr = inp;
    memcpy(mem + inp, t, len);
    inp = (uint32_t)((inp + len + 8) / 8 * 8);
    *n = (uint32_t)len;
    free(t);
}
static void hexof(char *dst, const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) sprintf(dst + 2 * i, "%02x", p[i]);
    dst[2 * n] = 0;
}
static void string_at(char *dst, uint32_t off) {
    uint32_t p = u32(off), n = u32(off + 4);
    memcpy(dst, mem + p, n);
    dst[n] = 0;
}
static int pflags(const char *s) { return strchr(s, 'f') != NULL; }
static int oflags(const char *s) {
    return (strchr(s, 'c') ? 1 : 0) | (strchr(s, 'd') ? 2 : 0) | (strchr(s, 'e') ? 4 : 0) | (strchr(s, 't') ? 8 : 0);
}
static int dflags(const char *s) {
    int v = 0;
    for (; *s; s++) {
        const char *q = strchr(FLAG_LETTERS, *s);
        if (q && *s) v |= 1 << (q - FLAG_LETTERS);
    }
    return v;
}
static const char *stat_text(uint32_t at) {
    snprintf(out, sizeof out, "type=%s size=%" PRIu64 " links=%" PRIu64, KINDS[mem[at]], u64(at + 16), u64(at + 8));
    return out;
}
static const char *stream_error(void) { return mem[R + 4] == 0 ? "stream-failed" : "stream-closed"; }

#define MAXH 64
static char hname[MAXH][32];
static uint32_t hval[MAXH];
static int nh;
static int hfind(const char *n) {
    for (int i = 0; i < nh; i++) if (!strcmp(hname[i], n)) return i;
    return -1;
}
static void hset(const char *n, uint32_t v) {
    int i = hfind(n);
    if (i < 0) i = nh++;
    snprintf(hname[i], sizeof hname[i], "%s", n);
    hval[i] = v;
}

static int cmpstr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static const char *op(char **t, int nt) {
    const char *o = t[0];
    int used[2] = {1, -1};
    if (!strcmp(o, "preopen")) used[0] = -1;
    else if (!strcmp(o, "open")) used[0] = 2;
    else if (!strcmp(o, "rename")) used[1] = 3;
    else if (!strcmp(o, "link")) used[1] = 4;
    else if (!strcmp(o, "same")) used[1] = 2;
    for (int k = 0; k < 2; k++)
        if (used[k] >= 0 && (used[k] >= nt || hfind(t[used[k]]) < 0)) return "no-handle";
#define D(i) hval[hfind(t[i])]
    uint32_t p, n, q, m;
    if (!strcmp(o, "preopen")) {
        clear();
        fusedwasi_wasi_filesystem_preopens_get_directories(&G, 0, R);
        uint32_t ptr = u32(R);
        int i = atoi(t[2]);
        hset(t[1], u32(ptr + 12 * i));
        static char buf[4096];
        string_at(buf, ptr + 12 * i + 4);
        snprintf(out, sizeof out, "name=%s", buf);
        return out;
    }
    if (!strcmp(o, "open")) {
        put(t[3], &p, &n);
        clear();
        FS(descriptor_open_at, D(2), pflags(t[4]), p, n, oflags(t[5]), dflags(t[6]), R);
        if (failed()) return err(4);
        hset(t[1], u32(R + 4));
        return "ok";
    }
    if (!strcmp(o, "stat")) {
        clear();
        FS(descriptor_stat, D(1), R);
        return failed() ? err(8) : stat_text(R + 8);
    }
    if (!strcmp(o, "stat-at")) {
        put(t[3], &p, &n);
        clear();
        FS(descriptor_stat_at, D(1), pflags(t[2]), p, n, R);
        return failed() ? err(8) : stat_text(R + 8);
    }
    if (!strcmp(o, "type")) {
        clear();
        FS(descriptor_get_type, D(1), R);
        return failed() ? err(1) : KINDS[mem[R + 1]];
    }
    if (!strcmp(o, "flags")) {
        clear();
        FS(descriptor_get_flags, D(1), R);
        if (failed()) return err(1);
        int bits = mem[R + 1], k = 0;
        for (int i = 0; FLAG_LETTERS[i]; i++) if (bits >> i & 1) out[k++] = FLAG_LETTERS[i];
        if (!k) out[k++] = '-';
        out[k] = 0;
        return out;
    }
    if (!strcmp(o, "read")) {
        clear();
        FS(descriptor_read, D(1), strtoull(t[3], 0, 10), strtoull(t[2], 0, 10), R);
        if (failed()) return err(4);
        uint32_t len = u32(R + 8);
        hexof(out, mem + u32(R + 4), len);
        snprintf(out + 2 * len, 32, " eof=%s", mem[R + 12] ? "true" : "false");
        return out;
    }
    if (!strcmp(o, "write")) {
        put(t[3], &p, &n);
        clear();
        FS(descriptor_write, D(1), p, n, strtoull(t[2], 0, 10), R);
        if (failed()) return err(8);
        snprintf(out, sizeof out, "written=%" PRIu64, u64(R + 8));
        return out;
    }
    if (!strcmp(o, "read-stream")) {
        clear();
        FS(descriptor_read_via_stream, D(1), strtoull(t[2], 0, 10), R);
        if (failed()) return err(4);
        uint32_t s = u32(R + 4);
        clear();
        IO(input_stream_blocking_read, s, strtoull(t[3], 0, 10), R);
        if (mem[R] == 1) snprintf(out, sizeof out, "%s", stream_error());
        else hexof(out, mem + u32(R + 4), u32(R + 8));
        fusedwasi_wasi_io_streams_input_stream_drop(&G, s);
        return out;
    }
    if (!strcmp(o, "write-stream") || !strcmp(o, "append-stream")) {
        int w = !strcmp(o, "write-stream");
        const char *data = w ? t[3] : t[2];
        clear();
        if (w) FS(descriptor_write_via_stream, D(1), strtoull(t[2], 0, 10), R);
        else FS(descriptor_append_via_stream, D(1), R);
        if (failed()) return err(4);
        uint32_t s = u32(R + 4);
        put(data, &p, &n);
        clear();
        IO(output_stream_blocking_write_and_flush, s, p, n, R);
        snprintf(out, sizeof out, "%s", mem[R] == 1 ? stream_error() : "ok");
        fusedwasi_wasi_io_streams_output_stream_drop(&G, s);
        return out;
    }
    if (!strcmp(o, "readdir")) {
        clear();
        FS(descriptor_read_directory, D(1), R);
        if (failed()) return err(4);
        uint32_t s = u32(R + 4);
        char *names[1024];
        int k = 0;
        for (;;) {
            clear();
            FS(directory_entry_stream_read_directory_entry, s, R);
            char buf[4400];
            if (mem[R] == 1) {
                snprintf(buf, sizeof buf, "err=%s", ERRS[mem[R + 4]]);
                names[k++] = strdup(buf);
                break;
            }
            if (!mem[R + 4]) break;
            char nm[4096];
            string_at(nm, R + 12);
            snprintf(buf, sizeof buf, "%s:%s", nm, KINDS[mem[R + 8]]);
            names[k++] = strdup(buf);
        }
        fusedwasi_wasi_filesystem_types_directory_entry_stream_drop(&G, s);
        qsort(names, k, sizeof *names, cmpstr);
        out[0] = 0;
        for (int i = 0; i < k; i++) strcat(out, i ? "," : ""), strcat(out, names[i]), free(names[i]);
        return out;
    }
    if (!strcmp(o, "mkdir") || !strcmp(o, "rmdir") || !strcmp(o, "unlink")) {
        put(t[2], &p, &n);
        clear();
        if (!strcmp(o, "mkdir")) FS(descriptor_create_directory_at, D(1), p, n, R);
        else if (!strcmp(o, "rmdir")) FS(descriptor_remove_directory_at, D(1), p, n, R);
        else FS(descriptor_unlink_file_at, D(1), p, n, R);
        return failed() ? err(1) : "ok";
    }
    if (!strcmp(o, "rename")) {
        put(t[2], &p, &n);
        put(t[4], &q, &m);
        clear();
        FS(descriptor_rename_at, D(1), p, n, D(3), q, m, R);
        return failed() ? err(1) : "ok";
    }
    if (!strcmp(o, "symlink")) {
        put(t[2], &p, &n);
        put(t[3], &q, &m);
        clear();
        FS(descriptor_symlink_at, D(1), p, n, q, m, R);
        return failed() ? err(1) : "ok";
    }
    if (!strcmp(o, "link")) {
        put(t[3], &p, &n);
        put(t[5], &q, &m);
        clear();
        FS(descriptor_link_at, D(1), pflags(t[2]), p, n, D(4), q, m, R);
        return failed() ? err(1) : "ok";
    }
    if (!strcmp(o, "readlink")) {
        put(t[2], &p, &n);
        clear();
        FS(descriptor_readlink_at, D(1), p, n, R);
        if (failed()) return err(4);
        static char buf[4096];
        string_at(buf, R + 4);
        snprintf(out, sizeof out, "target=%s", buf);
        return out;
    }
    if (!strcmp(o, "set-size")) {
        clear();
        FS(descriptor_set_size, D(1), strtoull(t[2], 0, 10), R);
        return failed() ? err(1) : "ok";
    }
    if (!strcmp(o, "sync") || !strcmp(o, "sync-data")) {
        clear();
        if (!strcmp(o, "sync")) FS(descriptor_sync, D(1), R);
        else FS(descriptor_sync_data, D(1), R);
        return failed() ? err(1) : "ok";
    }
    if (!strcmp(o, "advise")) {
        clear();
        FS(descriptor_advise, D(1), strtoull(t[2], 0, 10), strtoull(t[3], 0, 10), 0, R);
        return failed() ? err(1) : "ok";
    }
    if (!strcmp(o, "same")) return fusedwasi_wasi_filesystem_types_descriptor_is_same_object(&G, D(1), D(2)) ? "true" : "false";
    if (!strcmp(o, "hash") || !strcmp(o, "hash-at")) {
        int at = !strcmp(o, "hash-at");
        uint8_t first[16];
        if (at) put(t[3], &p, &n);
        for (int pass = 0; pass < 2; pass++) {
            clear();
            if (at) FS(descriptor_metadata_hash_at, D(1), pflags(t[2]), p, n, R);
            else FS(descriptor_metadata_hash, D(1), R);
            if (pass == 0) {
                if (failed()) return err(8);
                memcpy(first, mem + R + 8, 16);
            }
        }
        snprintf(out, sizeof out, "stable=%s", memcmp(first, mem + R + 8, 16) ? "false" : "true");
        return out;
    }
    if (!strcmp(o, "drop")) {
        int i = hfind(t[1]);
        fusedwasi_wasi_filesystem_types_descriptor_drop(&G, hval[i]);
        hname[i][0] = 0;
        hname[i][1] = 1; /* never matches */
        return "ok";
    }
    fprintf(stderr, "unknown op %s\n", o);
    exit(3);
}

int main(int argc, char **argv) {
    if (argc != 4) return fprintf(stderr, "usage: ours_c script rw ro\n"), 1;
    mem = calloc(1, MEMSIZE);
    struct fusedwasi_p2_preopen pre[2] = {{argv[2], "/world", 1}, {argv[3], "/ro", 0}};
    fusedwasi_p2_init(&S);
    S.preopens = pre, S.preopen_count = 2;
    fusedwasi_p2_guest_init(&G, &S);
    G.trap = trap;
    G.realloc = bump;
    fusedwasi_p2_set_memory(&G, 0, mem, MEMSIZE);
    FILE *f = fopen(argv[1], "r");
    if (!f) return perror("script"), 1;
    char line[4096];
    int count = 0;
    while (fgets(line, sizeof line, f)) {
        char *mark = strstr(line, "#~");
        if (mark) *mark = 0;
        char *toks[16];
        int nt = 0;
        for (char *tok = strtok(line, " \t\r\n"); tok && nt < 16; tok = strtok(NULL, " \t\r\n")) toks[nt++] = tok;
        if (!nt || toks[0][0] == '#') continue;
        count++;
        printf("%d %s: %s\n", count, toks[0], op(toks, nt));
    }
    return 0;
}
