# A TLS hook for `https` in C

Status: written with steps 3 and 4 of [S0001](specs/S0001.md); the reference hook of section 3 is built and run in the CI (job `tls-reference`, 2026-11-05). Facts are marked **[read]** (read in the code or the source), **[run]** (run by a test of the repository, in the CI) or **[not checked]**.

## 1. What it is

The C transport of `wasi:http` has no TLS code. An `https` request is `internal-error` unless the embedder sets `state->tls`, a table of four functions over the TLS library it has (`fusedwasi_p2_tls_ops`, in `wasi_p2_http.h`), or `state->http_send`, which replaces the whole transport and so needs an HTTP/1.1 client of its own. With `state->tls` the library keeps the socket, the HTTP/1.1, the timeouts of each phase, the limits and the errors, and the hook does the TLS over the socket **[read: `fusedwasi_p2_http_default_send`, `fusedwasi_p2__http_conn_*`]**.

The Python twin has TLS of its own (`ssl`, the system trust store) and **no hook**, deliberately ([S0001](specs/S0001.md) 3.3; `host_scope.md`, Known limits): to set its TLS an embedder replaces the transport with `http_send`.

## 2. The contract

The library calls the four functions and **cannot check what they do**. A hook that does not verify is a hook that makes `https` as safe as `http`. So the hook **must**:

1. verify the certificate chain against trust anchors that the embedder chose, **and** the host name (`host` is the authority of the request without the port; an IP address is given as text);
2. offer TLS 1.2 or later;
3. give each session its own state: the library may run several requests at once, each with its own session (`connect` is called for each request);
4. do its own waiting: `read` and `write` get a time in milliseconds and must not wait longer; a TLS library may hold bytes that it has decrypted already, so polling the descriptor is not enough;
5. report a failure as one of the kinds below, and send the close notification in `close` if it can.

| The hook reports | The guest sees |
| --- | --- |
| `FUSEDWASI_P2_TLS_CERTIFICATE` | `TLS-certificate-error` |
| `FUSEDWASI_P2_TLS_PROTOCOL` | `TLS-protocol-error` |
| `FUSEDWASI_P2_TLS_TIMEOUT` | `connection-timeout` (in `connect`), `connection-read-timeout` (in `read`), `connection-write-timeout` (in `write`) |
| `FUSEDWASI_P2_TLS_TERMINATED` | `connection-terminated` |
| `FUSEDWASI_P2_TLS_OTHER` (or 0, or an unknown kind) | `internal-error`, with the `message` of the hook |

`message` is used only for `internal-error`: the cases `TLS-certificate-error` and `TLS-protocol-error` of the WIT have no text. A `read` that returns 0 is the end of the session in order. A `read` that returns more than it was asked for, and a `write` that returns 0 or more than it was given, are failures of the hook and are answered as `internal-error` **[run: `tests/test_tls_hook.py`]**.

The descriptor that `connect` is given is connected and in **blocking** mode; the hook may change the mode for its session, and the library closes the descriptor after `close` **[read]**.

## 3. A hook over Mbed TLS, in full

This is `tests/tls_mbedtls/fusedwasi_mbedtls_hook.h`, **as it is**: a test (`test_the_document_shows_the_hook_as_it_is`) fails when the two differ. It is built and run in the CI against a real TLS server, with Mbed TLS 3.6.7 and a trimmed configuration, and the C twin (with this hook) and the Python twin (with `ssl`) give the same status and body, or the same `error-code`, for a short body, a body of 100 KB and a body that ends with the closing of the connection, by name and by IP address; for a certificate of another name, for a CA that is not trusted, for a server that speaks plain HTTP on the TLS port, for a server of TLS 1.1, for a handshake that gets no answer and for an answer that does not come **[run: `tests/test_tls_reference.py`, job `tls-reference` of `c-checks.yml`]**. The one file is for POSIX (Linux, macOS: `poll`, `fcntl`) and for Windows (`WSAPoll`, `ioctlsocket`): the few calls that differ are in one block at its top **[run: the job `tls-reference` is green on Linux, 32-bit Linux, macOS, Windows x64 and Windows ARM64 at `43f0602`; on Windows 13 of its 14 tests of `test_tls_reference.py` ran and the one with a server of TLS 1.1 was skipped, because the `openssl s_server` of the runner does not answer: that case is not checked on Windows]**.

What an embedder changes is marked EMBEDDER in the file: the trust anchors (here a PEM text in `state->user`; they may come from a file, from the system, or be pinned).

```c
/* A TLS hook (`fusedwasi_p2_tls_ops`, docs/specs/S0001.md) over Mbed TLS 3.6: the reference of the tests, and the example of docs/tls_hook.md.
   An embedder copies it and changes what is marked EMBEDDER. It builds on POSIX (Linux, macOS) and on Windows: the calls of the system that differ (waiting, the mode of the descriptor,
   the errors, the clock) are in one block, `ref_tls_*` below, with a branch for Windows.

   The contract that the hook keeps (docs/tls_hook.md, section 2): the chain and the host name are verified (required), TLS 1.2 or later, a failure is reported as one of the kinds,
   each session is its own (no state is shared between sessions), and the waiting is the hook's. */
#ifndef FUSEDWASI_MBEDTLS_HOOK_H
#define FUSEDWASI_MBEDTLS_HOOK_H

#include "wasi_p2_http.h" /* first: it brings the sockets of the system (on Windows winsock2.h, before windows.h) */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#endif

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

/* EMBEDDER: what the hook needs from the host. `state->user` points to one of these. The trust anchors are the embedder's: a PEM bundle, from a file, from the system, or pinned. */
typedef struct {
    const char *ca_pem; /* the trust anchors, a PEM text with a NUL at its end */
} ref_tls_config;

typedef struct {
    int fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
} ref_tls_session;

#ifdef _WIN32
#define REF_TLS_THREAD_LOCAL __declspec(thread)
#else
#define REF_TLS_THREAD_LOCAL _Thread_local
#endif

/* the text of a failure; the library copies it at once, so one buffer for each thread is enough */
static REF_TLS_THREAD_LOCAL char ref_tls_message[200];

/* ---- the calls of the system that differ between POSIX and Windows ---- */

static long long ref_tls_now_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

/* the descriptor that the library gives is an `int` (on Windows a SOCKET that fits in it) */
static int ref_tls_set_nonblocking(int fd) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &on);
#else
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
}

static int ref_tls_poll(int fd, short events, int ms) {
#ifdef _WIN32
    WSAPOLLFD p;
    p.fd = (SOCKET)fd, p.events = events, p.revents = 0;
    return WSAPoll(&p, 1, ms);
#else
    struct pollfd p = {fd, events, 0};
    return poll(&p, 1, ms);
#endif
}

static int ref_tls_error_is_interrupt(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

static int ref_tls_error_is_again(void) { /* a non-blocking call that has to wait */
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

static int ref_tls_error_is_reset(void) {
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAECONNRESET || e == WSAECONNABORTED;
#else
    return errno == ECONNRESET || errno == EPIPE;
#endif
}

static int ref_tls_send_bytes(int fd, const unsigned char *buf, size_t n) {
#ifdef _WIN32
    return send((SOCKET)fd, (const char *)buf, (int)n, 0);
#else
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL; /* a write to a closed connection is an error and not a signal */
#endif
    return (int)send(fd, buf, n, flags);
#endif
}

static int ref_tls_recv_bytes(int fd, unsigned char *buf, size_t n) {
#ifdef _WIN32
    return recv((SOCKET)fd, (char *)buf, (int)n, 0);
#else
    return (int)recv(fd, buf, n, 0);
#endif
}

/* ---- the hook ---- */

/* waits for the descriptor until `deadline`: 1 ready, 0 the time is over, -1 failure */
static int ref_tls_wait(int fd, short events, long long deadline) {
    for (;;) {
        long long left = deadline - ref_tls_now_ms();
        if (left <= 0) return 0;
        int r = ref_tls_poll(fd, events, left > 2000000000 ? 2000000000 : (int)left);
        if (r < 0 && ref_tls_error_is_interrupt()) continue;
        return r;
    }
}

/* the two callbacks that give Mbed TLS the socket (non-blocking: "again" is "want read" or "want write") */
static int ref_tls_send(void *ctx, const unsigned char *buf, size_t n) {
    int r = ref_tls_send_bytes(((ref_tls_session *)ctx)->fd, buf, n);
    if (r >= 0) return r;
    if (ref_tls_error_is_again()) return MBEDTLS_ERR_SSL_WANT_WRITE;
    if (ref_tls_error_is_reset()) return MBEDTLS_ERR_NET_CONN_RESET;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int ref_tls_recv(void *ctx, unsigned char *buf, size_t n) {
    int r = ref_tls_recv_bytes(((ref_tls_session *)ctx)->fd, buf, n);
    if (r >= 0) return r;
    if (ref_tls_error_is_again()) return MBEDTLS_ERR_SSL_WANT_READ;
    if (ref_tls_error_is_reset()) return MBEDTLS_ERR_NET_CONN_RESET;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static void ref_tls_report(fusedwasi_p2_tls_error *err, int kind, const char *text) {
    err->kind = kind;
    err->message = text;
}

/* what a failed call of Mbed TLS is, in the kinds of the hook */
static void ref_tls_fail(fusedwasi_p2_tls_error *err, int ret) {
    int kind;
    switch (ret) {
    case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED: kind = FUSEDWASI_P2_TLS_CERTIFICATE; break;
    case MBEDTLS_ERR_NET_CONN_RESET:
    case MBEDTLS_ERR_NET_SEND_FAILED:
    case MBEDTLS_ERR_NET_RECV_FAILED:
    case MBEDTLS_ERR_SSL_CONN_EOF:
    case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY: kind = FUSEDWASI_P2_TLS_TERMINATED; break;
    case MBEDTLS_ERR_SSL_ALLOC_FAILED:
    case MBEDTLS_ERR_SSL_INTERNAL_ERROR:
    case MBEDTLS_ERR_SSL_BAD_INPUT_DATA: kind = FUSEDWASI_P2_TLS_OTHER; break;
    default: kind = FUSEDWASI_P2_TLS_PROTOCOL; break; /* the handshake or the records went wrong: no common version or suite, bad record, a fatal alert, ... */
    }
    mbedtls_strerror(ret, ref_tls_message, sizeof ref_tls_message);
    ref_tls_report(err, kind, ref_tls_message);
}

static void ref_tls_free(ref_tls_session *t) {
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_x509_crt_free(&t->ca);
    mbedtls_ctr_drbg_free(&t->drbg);
    mbedtls_entropy_free(&t->entropy);
    free(t);
}

static void *ref_tls_connect(fusedwasi_p2_state *s, int fd, const char *host, int timeout_ms, fusedwasi_p2_tls_error *err) {
    const ref_tls_config *cfg = (const ref_tls_config *)s->user;
    if (!cfg || !cfg->ca_pem || !*cfg->ca_pem) {
        /* no trust anchors: the hook does not fall back to "no check" */
        ref_tls_report(err, FUSEDWASI_P2_TLS_CERTIFICATE, "no trust anchors: set ca_pem of the config of the hook");
        return NULL;
    }
    ref_tls_session *t = (ref_tls_session *)calloc(1, sizeof *t);
    if (!t) {
        ref_tls_report(err, FUSEDWASI_P2_TLS_OTHER, "out of memory");
        return NULL;
    }
    t->fd = fd;
    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_x509_crt_init(&t->ca);
    mbedtls_entropy_init(&t->entropy);
    mbedtls_ctr_drbg_init(&t->drbg);
    int ret = (int)psa_crypto_init();
    if (ret == 0) ret = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy, NULL, 0);
    if (ret == 0) ret = mbedtls_x509_crt_parse(&t->ca, (const unsigned char *)cfg->ca_pem, strlen(cfg->ca_pem) + 1);
    if (ret == 0) ret = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        ref_tls_fail(err, ret);
        err->kind = FUSEDWASI_P2_TLS_OTHER;
        ref_tls_free(t);
        return NULL;
    }
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED); /* the certificate and the host name are checked: there is no other mode here */
    mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, NULL);
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);
    mbedtls_ssl_conf_min_tls_version(&t->conf, MBEDTLS_SSL_VERSION_TLS1_2);
    ret = mbedtls_ssl_setup(&t->ssl, &t->conf);
    if (ret == 0) ret = mbedtls_ssl_set_hostname(&t->ssl, host); /* for SNI, and for the check of the name */
    if (ret != 0) {
        ref_tls_fail(err, ret);
        ref_tls_free(t);
        return NULL;
    }
#ifdef SO_NOSIGPIPE /* macOS has no MSG_NOSIGNAL: the socket itself must not raise a signal on a write to a closed connection */
    {
        int one = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (const void *)&one, sizeof one);
    }
#endif
    ref_tls_set_nonblocking(fd); /* the hook does its own waiting */
    mbedtls_ssl_set_bio(&t->ssl, t, ref_tls_send, ref_tls_recv, NULL);
    long long deadline = ref_tls_now_ms() + timeout_ms;
    for (;;) {
        ret = mbedtls_ssl_handshake(&t->ssl);
        if (ret == 0) return t;
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ref_tls_fail(err, ret);
            ref_tls_free(t);
            return NULL;
        }
        int w = ref_tls_wait(fd, ret == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT, deadline);
        if (w <= 0) {
            ref_tls_report(err, w == 0 ? FUSEDWASI_P2_TLS_TIMEOUT : FUSEDWASI_P2_TLS_TERMINATED, NULL);
            ref_tls_free(t);
            return NULL;
        }
    }
}

static int ref_tls_read(void *session, uint8_t *buf, size_t n, int timeout_ms, fusedwasi_p2_tls_error *err) {
    ref_tls_session *t = (ref_tls_session *)session;
    long long deadline = ref_tls_now_ms() + timeout_ms;
    for (;;) {
        int ret = mbedtls_ssl_read(&t->ssl, buf, n); /* bytes that Mbed TLS already holds are given before the descriptor is looked at */
        if (ret > 0) return ret;
        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0; /* the peer closed the session in order */
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ref_tls_fail(err, ret);
            return -1;
        }
        int w = ref_tls_wait(t->fd, ret == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT, deadline);
        if (w <= 0) {
            ref_tls_report(err, w == 0 ? FUSEDWASI_P2_TLS_TIMEOUT : FUSEDWASI_P2_TLS_TERMINATED, NULL);
            return -1;
        }
    }
}

static int ref_tls_write(void *session, const uint8_t *buf, size_t n, int timeout_ms, fusedwasi_p2_tls_error *err) {
    ref_tls_session *t = (ref_tls_session *)session;
    long long deadline = ref_tls_now_ms() + timeout_ms;
    for (;;) {
        int ret = mbedtls_ssl_write(&t->ssl, buf, n);
        if (ret > 0) return ret;
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ref_tls_fail(err, ret == 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : ret);
            return -1;
        }
        int w = ref_tls_wait(t->fd, ret == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT, deadline);
        if (w <= 0) {
            ref_tls_report(err, w == 0 ? FUSEDWASI_P2_TLS_TIMEOUT : FUSEDWASI_P2_TLS_TERMINATED, NULL);
            return -1;
        }
    }
}

static void ref_tls_close(void *session) {
    ref_tls_session *t = (ref_tls_session *)session;
    (void)mbedtls_ssl_close_notify(&t->ssl); /* one try, on a non-blocking descriptor: a failure here changes nothing for the guest */
    ref_tls_free(t);
}

static const fusedwasi_p2_tls_ops ref_tls_ops = {ref_tls_connect, ref_tls_read, ref_tls_write, ref_tls_close};

#endif
```

To use it:

```c
ref_tls_config cfg = {ca_pem_text};   /* a NUL-terminated PEM bundle */
fusedwasi_p2_state state;
fusedwasi_p2_init(&state);
state.user = &cfg;                    /* the hook reads its configuration from here */
state.tls = &ref_tls_ops;             /* https now goes through the hook */
```

### Building it

`tests/tls_mbedtls/CMakeLists.txt` is the example: it fetches Mbed TLS with `FetchContent_MakeAvailable`, pinned by the URL of the release archive and its SHA-256 (a tag can move, a hash cannot), static, with `tests/tls_mbedtls/mbedtls_trim.h` as the configuration (`MBEDTLS_CONFIG_FILE`): a client of TLS 1.2, ECDHE with RSA and ECDSA certificates, no server, no DTLS, no TLS 1.3, no PSK. **[run]** On Linux x86-64 with the 3.6.2 release the default configuration made a client of 674 KB (stripped) and the trimmed one 412 KB, an empty program being 14 KB ([S0002](specs/S0002.md) 4.1; the pin is now 3.6.7 and the trimmed list is the same). Mbed TLS is dual licensed, Apache-2.0 or GPL-2.0-or-later **[read: `LICENSE` of the 3.6.7 archive]**; the library does not ship it, the embedder links it and takes its terms. A project that has Mbed TLS already uses `find_package(MbedTLS)` instead of the fetch (two copies in one program give duplicate symbols) **[not checked]**.

## 4. Other TLS libraries **[not checked]**

Nobody has run these; they say what the four functions are made of.

* **OpenSSL:** `connect`: `SSL_CTX_new(TLS_client_method())`, `SSL_CTX_set_min_proto_version(TLS1_2_VERSION)`, `SSL_CTX_set_verify(SSL_VERIFY_PEER)`, load the anchors, `SSL_new`, `SSL_set_fd`, `SSL_set_tlsext_host_name` (SNI) **and** `SSL_set1_host` (the check of the name), `SSL_connect`; `read` and `write` with `SSL_read` and `SSL_write`, and `poll` on `SSL_ERROR_WANT_READ` and `SSL_ERROR_WANT_WRITE`; `SSL_get_verify_result` tells a certificate error from a protocol error; `close`: `SSL_shutdown`, `SSL_free`.
* **wolfSSL:** the same shape with `wolfSSL_connect`, `wolfSSL_read`, `wolfSSL_write` and `wolfSSL_check_domain_name`. Mind its licence (GPL or commercial).
* **Windows (Schannel) and macOS (Security framework, Network.framework), natively:** the system libraries do the check with the system store; the hook is the same four functions over `InitializeSecurityContext` and `EncryptMessage` / `DecryptMessage`, or over Network.framework. They are bigger to write than the three above.

## 5. What a hook is not

It is not a TLS server (the `incoming-handler` side of `wasi:http` is the embedder's), and it has no ALPN, no client certificate and no session resumption in this version. HTTP/1.1 is the only protocol that the library speaks over it.
