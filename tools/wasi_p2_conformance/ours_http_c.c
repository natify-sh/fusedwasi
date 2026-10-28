#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS /* strcat, strtok and fopen as they are */
#define _CRT_NONSTDC_NO_WARNINGS /* strdup as it is */
#endif
/* The C host of fusedwasi (include/fusedwasi/p2/wasi_p2_http.h) driven by script_http.txt: a port of ours_http.py, printing the same lines,
 * except that an error-code is printed as its number ("err=#12"): runner.py turns it into the name with the table of the Python twin.
 *
 *   cc -I src/fusedwasi/include/fusedwasi/p2 -o ours_http_c tools/wasi_p2_conformance/ours_http_c.c
 *   ours_http_c script_http.txt        (PORT already replaced)
 *
 * Not part of the tests.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasi_p2_http.h"
#include "wasi_p2_io.h"

#define R 256
#define MEMSIZE (1 << 20)

static fusedwasi_p2_state S;
static fusedwasi_p2_guest G;
static uint8_t *mem;
static uint32_t free_at = 0x80000, in_at = 0x1000;

#define H(fn, ...) fusedwasi_wasi_http_types_##fn(&G, __VA_ARGS__)
#define HC(fn, ...) fusedwasi_wasi_http_types_##fn(&G, 0, __VA_ARGS__)
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
static int allow(fusedwasi_p2_state *st, const char *scheme, const char *authority) {
    (void)st, (void)scheme, (void)authority;
    return 1;
}

static uint32_t u32(uint32_t off) { uint32_t v; memcpy(&v, mem + off, 4); return v; }
static void st32(uint32_t off, uint32_t v) { memcpy(mem + off, &v, 4); }
static void clear(void) { memset(mem + R, 0, 128); }
static uint32_t put(const void *data, size_t len) {
    uint32_t ptr = in_at;
    memcpy(mem + in_at, data, len);
    in_at = (uint32_t)((in_at + len + 8) / 8 * 8);
    return ptr;
}

static char out[1 << 20];

static const char *error(uint32_t off) {
    snprintf(out, sizeof out, "err=#%u", mem[off]);
    return out;
}

struct kv { const char *k; char *v; };

static uint32_t fields(struct kv *items, int n) {
    uint32_t arr = put(calloc(16 * (n ? n : 1), 1), 16 * (size_t)n);
    for (int i = 0; i < n; i++) {
        uint32_t np = put(items[i].k, strlen(items[i].k)), vp = put(items[i].v, strlen(items[i].v));
        st32(arr + 16 * i, np), st32(arr + 16 * i + 4, (uint32_t)strlen(items[i].k));
        st32(arr + 16 * i + 8, vp), st32(arr + 16 * i + 12, (uint32_t)strlen(items[i].v));
    }
    clear();
    HC(fields_from_list, arr, (uint32_t)n, R);
    if (mem[R] != 0) { fprintf(stderr, "fields_from_list failed\n"); exit(3); }
    return u32(R + 4);
}

static int cmpstr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static char *entries(uint32_t handle) {
    clear();
    HC(fields_entries, handle, R);
    uint32_t ptr = u32(R), n = u32(R + 4);
    char *list[256];
    int k = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t e = ptr + 16 * i;
        char *item = malloc(u32(e + 4) + u32(e + 12) + 2);
        memcpy(item, mem + u32(e), u32(e + 4));
        item[u32(e + 4)] = 0;
        if (!strcmp(item, "date") || !strcmp(item, "server")) { free(item); continue; }
        item[u32(e + 4)] = ':';
        memcpy(item + u32(e + 4) + 1, mem + u32(e + 8), u32(e + 12));
        item[u32(e + 4) + 1 + u32(e + 12)] = 0;
        list[k++] = item;
    }
    qsort(list, k, sizeof *list, cmpstr);
    char *res = calloc(1, 1 << 16);
    for (int i = 0; i < k; i++) {
        if (i) strcat(res, ",");
        strcat(res, list[i]);
        free(list[i]);
    }
    return res;
}

static const char *STD[] = {"GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH"};

static const char *request(char **t, int nt) {
    struct kv headers[32], trailers[32];
    int nh = 0, ntr = 0;
    char *body = NULL;
    long first = -1;
    for (int i = 4; i < nt; i++) {
        char *opt = t[i], *eq;
        if (!strncmp(opt, "body=", 5)) {
            body = strdup(opt + 5);
            for (char *r = body, *w = body;; r++) {  /* \n to a newline */
                if (r[0] == '\\' && r[1] == 'n') *w++ = '\n', r++;
                else if (!(*w++ = *r)) break;
            }
        } else if (!strncmp(opt, "h:", 2) && (eq = strchr(opt, '='))) {
            *eq = 0;
            headers[nh].k = opt + 2, headers[nh++].v = eq + 1;
        } else if (!strncmp(opt, "tr:", 3) && (eq = strchr(opt, '='))) {
            *eq = 0;
            trailers[ntr].k = opt + 3, trailers[ntr++].v = eq + 1;
        } else if (!strncmp(opt, "first=", 6)) first = atol(opt + 6);
    }
    uint32_t req = H(outgoing_request_new, fields(headers, nh));
    const char *method = t[1];
    int std = -1;
    for (int i = 0; i < 9; i++) if (!strcmp(STD[i], method)) std = i;
    if (std >= 0) HC(outgoing_request_set_method, req, (uint32_t)std, 0, 0);
    else HC(outgoing_request_set_method, req, 9, put(method, strlen(method)), (uint32_t)strlen(method));
    HC(outgoing_request_set_scheme, req, 1, 0, 0, 0);
    HC(outgoing_request_set_authority, req, 1, put(t[2], strlen(t[2])), (uint32_t)strlen(t[2]));
    HC(outgoing_request_set_path_with_query, req, 1, put(t[3], strlen(t[3])), (uint32_t)strlen(t[3]));
    if (body || ntr) {
        clear();
        HC(outgoing_request_body, req, R);
        uint32_t ob = u32(R + 4);
        if (body) {
            clear();
            HC(outgoing_body_write, ob, R);
            uint32_t stream = u32(R + 4);
            clear();
            IO(output_stream_blocking_write_and_flush, stream, put(body, strlen(body)), (uint32_t)strlen(body), R);
            fusedwasi_wasi_io_streams_output_stream_drop(&G, stream);
        }
        uint32_t tf = ntr ? fields(trailers, ntr) : 0;
        clear();
        HC(outgoing_body_finish, ob, ntr ? 1 : 0, tf, R);
    }
    uint32_t opts = 0;
    if (first >= 0) {
        opts = fusedwasi_wasi_http_types_request_options_new(&G);
        H(request_options_set_first_byte_timeout, opts, 1, (uint64_t)first * 1000000ULL);
    }
    clear();
    fusedwasi_wasi_http_outgoing_handler_handle(&G, 0, req, opts ? 1 : 0, opts, R);
    if (mem[R] == 1) return error(R + 8);
    uint32_t fut = u32(R + 8);
    uint32_t pol = H(future_incoming_response_subscribe, fut);
    fusedwasi_wasi_io_poll_pollable_block(&G, pol);
    clear();
    HC(future_incoming_response_get, fut, R);
    if (mem[R + 16] == 1) return error(R + 24);
    uint32_t resp = u32(R + 24);
    uint32_t status = H(incoming_response_status, resp);
    char *h = entries(H(incoming_response_headers, resp));
    clear();
    HC(incoming_response_consume, resp, R);
    uint32_t ib = u32(R + 4);
    clear();
    HC(incoming_body_stream, ib, R);
    uint32_t stream = u32(R + 4);
    static uint8_t data[1 << 19];
    size_t dn = 0;
    for (;;) {
        clear();
        IO(input_stream_blocking_read, stream, 4096, R);
        if (mem[R] == 1) break;
        memcpy(data + dn, mem + u32(R + 4), u32(R + 8));
        dn += u32(R + 8);
    }
    fusedwasi_wasi_io_streams_input_stream_drop(&G, stream);
    uint32_t ft = H(incoming_body_finish, ib);
    clear();
    HC(future_trailers_get, ft, R);
    char *tr = mem[R + 24] == 1 ? entries(u32(R + 28)) : strdup("none");
    int k = snprintf(out, sizeof out, "status=%u h=[%s] body=", status, h);
    for (size_t i = 0; i < dn; i++) k += sprintf(out + k, "%02x", data[i]);
    snprintf(out + k, sizeof out - k, " trailers=%s", tr);
    return out;
}

int main(int argc, char **argv) {
    if (argc != 2) return fprintf(stderr, "usage: ours_http_c script\n"), 1;
    mem = calloc(1, MEMSIZE);
    fusedwasi_p2_init(&S);
    S.http_check = allow;
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
        char *toks[32];
        int nt = 0;
        for (char *tok = strtok(line, " \t\r\n"); tok && nt < 32; tok = strtok(NULL, " \t\r\n")) toks[nt++] = tok;
        if (!nt || toks[0][0] == '#') continue;
        count++;
        printf("%d req: %s\n", count, request(toks, nt));
        fflush(stdout);
    }
    return 0;
}
