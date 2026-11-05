/* S0001, step 2: the TLS hook of the C transport, tried with a hook that has no TLS. It answers from a script, so that what is tested is how the library uses the hook and how it maps
   what the hook reports to the error-code of wasi:http (tests/test_tls_hook.py).
   Usage: driver <scenario> <port> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasi_p2_http.h"

static const char *scenario;
static int connects, reads, writes, closes, fd_ok;
static char hook_host[128];
static char captured[4096];
static size_t captured_len, response_pos;
static char session_token;

static const char RESPONSE[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
static const char TRUNCATED[] = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc";

static void report(fusedwasi_p2_tls_error *e, int kind, const char *message) {
    e->kind = kind;
    e->message = message;
}

static int is(const char *name) { return strcmp(scenario, name) == 0; }

static void *h_connect(fusedwasi_p2_state *s, int fd, const char *host, int timeout_ms, fusedwasi_p2_tls_error *e) {
    (void)s;
    connects++;
    snprintf(hook_host, sizeof hook_host, "%s", host);
    fd_ok = fd >= 0 && timeout_ms > 0;
    if (is("connect_certificate")) return report(e, FUSEDWASI_P2_TLS_CERTIFICATE, "the name does not match"), NULL;
    if (is("connect_protocol")) return report(e, FUSEDWASI_P2_TLS_PROTOCOL, "no common version"), NULL;
    if (is("connect_timeout")) return report(e, FUSEDWASI_P2_TLS_TIMEOUT, NULL), NULL;
    if (is("connect_terminated")) return report(e, FUSEDWASI_P2_TLS_TERMINATED, NULL), NULL;
    if (is("connect_other")) return report(e, FUSEDWASI_P2_TLS_OTHER, "the library said no"), NULL;
    if (is("connect_unset_kind")) return report(e, 0, NULL), NULL;
    return &session_token;
}

static int h_read(void *session, uint8_t *buf, size_t n, int timeout_ms, fusedwasi_p2_tls_error *e) {
    (void)session, (void)timeout_ms;
    reads++;
    if (is("read_timeout")) return report(e, FUSEDWASI_P2_TLS_TIMEOUT, NULL), -1;
    if (is("read_certificate")) return report(e, FUSEDWASI_P2_TLS_CERTIFICATE, NULL), -1;
    if (is("read_protocol")) return report(e, FUSEDWASI_P2_TLS_PROTOCOL, NULL), -1;
    if (is("read_terminated")) return report(e, FUSEDWASI_P2_TLS_TERMINATED, NULL), -1;
    if (is("read_other")) return report(e, FUSEDWASI_P2_TLS_OTHER, "a failure of the record layer"), -1;
    if (is("read_other_no_message")) return report(e, FUSEDWASI_P2_TLS_OTHER, NULL), -1;
    if (is("read_too_much")) return (int)n + 1;
    const char *src = is("truncated") ? TRUNCATED : RESPONSE;
    size_t have = strlen(src) - response_pos, give = have < n ? have : n;
    if (give > 20) give = 20; /* in pieces, as a TLS library gives records */
    memcpy(buf, src + response_pos, give);
    response_pos += give;
    return (int)give;
}

static int h_write(void *session, const uint8_t *buf, size_t n, int timeout_ms, fusedwasi_p2_tls_error *e) {
    (void)session, (void)timeout_ms;
    writes++;
    if (is("write_timeout")) return report(e, FUSEDWASI_P2_TLS_TIMEOUT, NULL), -1;
    if (is("write_terminated")) return report(e, FUSEDWASI_P2_TLS_TERMINATED, NULL), -1;
    if (is("write_zero")) return 0;
    if (is("write_too_much")) return (int)n + 1;
    size_t give = n < 16 ? n : 16; /* partial writes: the library has to go on */
    if (captured_len + give < sizeof captured) memcpy(captured + captured_len, buf, give), captured_len += give;
    return (int)give;
}

static void h_close(void *session) {
    (void)session;
    closes++;
}

static const fusedwasi_p2_tls_ops OPS = {h_connect, h_read, h_write, h_close};

int main(int argc, char **argv) {
    scenario = argc > 1 ? argv[1] : "ok";
    const char *port = argc > 2 ? argv[2] : "0";
    int plain = is("http_untouched") || is("plain_truncated");
    char authority[64];
    snprintf(authority, sizeof authority, "127.0.0.1:%s", port);

    fusedwasi_p2_state state;
    fusedwasi_p2_init(&state);
    if (!is("unset")) state.tls = &OPS;

    fusedwasi_p2_http_request req;
    fusedwasi_p2_http_response out;
    fusedwasi_p2_http_error err;
    memset(&req, 0, sizeof req), memset(&out, 0, sizeof out), memset(&err, 0, sizeof err);
    req.method = "GET";
    req.scheme = plain ? "http" : "https";
    req.authority = authority;
    req.path = is("plain_truncated") ? "/truncated" : "/x";

    int rc = fusedwasi_p2_http_default_send(&state, &req, &out, &err);
    if (rc == 0) {
        printf("ok status=%u body=%.*s\n", (unsigned)out.status, (int)(out.body ? out.body->len : 0), out.body ? (const char *)out.body->data : "");
        fusedwasi_p2_http_fields_unref(out.headers);
        if (out.body) fusedwasi_p2_http_blob_unref(out.body);
    } else {
        printf("error tag=%u message=%s\n", (unsigned)err.tag, err.str[0] ? err.str[0] : "-");
        fusedwasi_p2_http_error_free(&err);
    }
    printf("calls connect=%d read=%d write=%d close=%d fd_ok=%d host=%s\n", connects, reads > 0, writes > 0, closes, fd_ok, hook_host);
    printf("request=");
    for (size_t i = 0; i < captured_len; i++) {
        if (captured[i] == '\r') fputs("\\r", stdout);
        else if (captured[i] == '\n') fputs("\\n", stdout);
        else putchar(captured[i]);
    }
    printf("\n");
    fusedwasi_p2_free(&state);
    return 0;
}
