/* One https request through the library, with the reference hook over Mbed TLS (docs/specs/S0001.md, tests/test_tls_reference.py).
   Usage: driver --authority host:port --path /x [--ca file] [--connect-ms N] [--first-ms N] [--between-ms N]
   Prints `ok status=N body=<hex>` or `error tag=N`; the tag is the index of the case in the error-code of wasi:http, as the Python twin counts them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fusedwasi_mbedtls_hook.h"

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *text = (char *)malloc(1 << 16);
    size_t n = text ? fread(text, 1, (1 << 16) - 1, f) : 0;
    fclose(f);
    if (text) text[n] = 0;
    return text;
}

int main(int argc, char **argv) {
    const char *authority = "127.0.0.1:443", *path = "/", *ca_file = NULL;
    long ms[3] = {-1, -1, -1};
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--authority")) authority = argv[i + 1];
        else if (!strcmp(argv[i], "--path")) path = argv[i + 1];
        else if (!strcmp(argv[i], "--ca")) ca_file = argv[i + 1];
        else if (!strcmp(argv[i], "--connect-ms")) ms[0] = atol(argv[i + 1]);
        else if (!strcmp(argv[i], "--first-ms")) ms[1] = atol(argv[i + 1]);
        else if (!strcmp(argv[i], "--between-ms")) ms[2] = atol(argv[i + 1]);
    }
    ref_tls_config cfg = {ca_file ? slurp(ca_file) : NULL};

    fusedwasi_p2_state state;
    fusedwasi_p2_init(&state);
    state.user = &cfg;
    state.tls = &ref_tls_ops;

    fusedwasi_p2_http_request req;
    fusedwasi_p2_http_response out;
    fusedwasi_p2_http_error err;
    memset(&req, 0, sizeof req), memset(&out, 0, sizeof out), memset(&err, 0, sizeof err);
    req.method = "GET", req.scheme = "https", req.authority = authority, req.path = path;
    for (int i = 0; i < 3; i++)
        if (ms[i] >= 0) req.has_timeout[i] = 1, req.timeout_ns[i] = (uint64_t)ms[i] * 1000000u;

    int rc = fusedwasi_p2_http_default_send(&state, &req, &out, &err);
    if (rc == 0) {
        printf("ok status=%u body=", (unsigned)out.status);
        for (uint64_t i = 0; out.body && i < out.body->len; i++) printf("%02x", out.body->data[i]);
        printf("\n");
        fusedwasi_p2_http_fields_unref(out.headers);
        if (out.body) fusedwasi_p2_http_blob_unref(out.body);
    } else {
        printf("error tag=%u\n", (unsigned)err.tag);
        fusedwasi_p2_http_error_free(&err);
    }
    free((void *)cfg.ca_pem);
    fusedwasi_p2_free(&state);
    return 0;
}
