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
