/* wasi:sockets (58 imports): network, instance-network, ip-name-lookup, tcp, tcp-create-socket, udp, udp-create-socket (the twin of p2/wasi_p2_sockets.py, where the
 * rules, the states and the layouts are described). BSD sockets, or Winsock on Windows (link ws2_32). Nothing is allowed unless `state->socket_addr_check` / `state->allow_ip_name_lookup` say so.
 * A socket is shared by its streams and its handle (a count of references), so a stream can outlive the handle of the socket; the descriptor is closed with the handle. */
#ifndef FUSEDWASI_P2_SOCKETS_H
#define FUSEDWASI_P2_SOCKETS_H

#include "wasi_p2_net.h"
#include "wasi_p2_io.h"

/* wasi:sockets/network error-code, in the order of the WIT */
enum {
    FUSEDWASI_P2_N_UNKNOWN, FUSEDWASI_P2_N_ACCESS_DENIED, FUSEDWASI_P2_N_NOT_SUPPORTED, FUSEDWASI_P2_N_INVALID_ARGUMENT, FUSEDWASI_P2_N_OUT_OF_MEMORY, FUSEDWASI_P2_N_TIMEOUT,
    FUSEDWASI_P2_N_CONCURRENCY_CONFLICT, FUSEDWASI_P2_N_NOT_IN_PROGRESS, FUSEDWASI_P2_N_WOULD_BLOCK, FUSEDWASI_P2_N_INVALID_STATE, FUSEDWASI_P2_N_NEW_SOCKET_LIMIT,
    FUSEDWASI_P2_N_ADDRESS_NOT_BINDABLE, FUSEDWASI_P2_N_ADDRESS_IN_USE, FUSEDWASI_P2_N_REMOTE_UNREACHABLE, FUSEDWASI_P2_N_CONNECTION_REFUSED, FUSEDWASI_P2_N_CONNECTION_RESET,
    FUSEDWASI_P2_N_CONNECTION_ABORTED, FUSEDWASI_P2_N_DATAGRAM_TOO_LARGE, FUSEDWASI_P2_N_NAME_UNRESOLVABLE, FUSEDWASI_P2_N_TEMPORARY_RESOLVER_FAILURE,
    FUSEDWASI_P2_N_PERMANENT_RESOLVER_FAILURE
};
#define FUSEDWASI_P2_NOK (-1) /* what the functions of this file return when nothing failed; a network error-code otherwise */

enum { FUSEDWASI_P2_SS_UNBOUND, FUSEDWASI_P2_SS_BIND_IN_PROGRESS, FUSEDWASI_P2_SS_BOUND, FUSEDWASI_P2_SS_LISTEN_IN_PROGRESS, FUSEDWASI_P2_SS_LISTENING,
       FUSEDWASI_P2_SS_CONNECT_IN_PROGRESS, FUSEDWASI_P2_SS_CONNECTED, FUSEDWASI_P2_SS_CLOSED };

#define FUSEDWASI_P2_SOCK_CHUNK 65536u
#define FUSEDWASI_P2_CHECK_SEND_PERMIT 16u
#define FUSEDWASI_P2_BACKLOG_MAX 4096u


FUSEDWASI_P2_FN int fusedwasi_p2_net_errno(int e) {
    switch (e) {
    case EACCES: case EPERM: return FUSEDWASI_P2_N_ACCESS_DENIED;
    case EAFNOSUPPORT: case EOPNOTSUPP: case ENOPROTOOPT: case EPROTONOSUPPORT: return FUSEDWASI_P2_N_NOT_SUPPORTED;
    case ENOMEM: case ENOBUFS: return FUSEDWASI_P2_N_OUT_OF_MEMORY;
    case ETIMEDOUT: return FUSEDWASI_P2_N_TIMEOUT;
    case EALREADY: return FUSEDWASI_P2_N_CONCURRENCY_CONFLICT;
    case EAGAIN: case EINPROGRESS: return FUSEDWASI_P2_N_WOULD_BLOCK;
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK: return FUSEDWASI_P2_N_WOULD_BLOCK;
#endif
    case EMFILE: case ENFILE: return FUSEDWASI_P2_N_NEW_SOCKET_LIMIT;
    case EADDRNOTAVAIL: return FUSEDWASI_P2_N_ADDRESS_NOT_BINDABLE;
    case EADDRINUSE: return FUSEDWASI_P2_N_ADDRESS_IN_USE;
    case EHOSTUNREACH: case ENETUNREACH: case ENETDOWN: return FUSEDWASI_P2_N_REMOTE_UNREACHABLE;
#ifdef EHOSTDOWN
    case EHOSTDOWN: return FUSEDWASI_P2_N_REMOTE_UNREACHABLE;
#endif
    case ECONNREFUSED: return FUSEDWASI_P2_N_CONNECTION_REFUSED;
    case ECONNRESET: case EPIPE: return FUSEDWASI_P2_N_CONNECTION_RESET;
    case ECONNABORTED: return FUSEDWASI_P2_N_CONNECTION_ABORTED;
    case EMSGSIZE: return FUSEDWASI_P2_N_DATAGRAM_TOO_LARGE;
    case ENOTCONN: case EISCONN: return FUSEDWASI_P2_N_INVALID_STATE;
    case EINVAL: return FUSEDWASI_P2_N_INVALID_ARGUMENT;
    default: return FUSEDWASI_P2_N_UNKNOWN;
    }
}

/* an address: family 0 is IPv4 (v4: four bytes), 1 is IPv6 (v6: eight 16-bit numbers) */
typedef struct {
    int family;
    uint16_t port;
    uint8_t v4[4];
    uint16_t v6[8];
    uint32_t flow, scope;
} fusedwasi_p2_addr;

/* from the flat arguments: the tag and eleven numbers */
FUSEDWASI_P2_FN fusedwasi_p2_addr fusedwasi_p2_addr_flat(uint32_t tag, const uint32_t *f) {
    fusedwasi_p2_addr a;
    memset(&a, 0, sizeof a);
    a.family = tag == 0 ? 0 : 1;
    a.port = (uint16_t)f[0];
    if (tag == 0) {
        for (int i = 0; i < 4; i++) a.v4[i] = (uint8_t)f[1 + i];
    } else {
        a.flow = f[1];
        for (int i = 0; i < 8; i++) a.v6[i] = (uint16_t)f[2 + i];
        a.scope = f[10];
    }
    return a;
}

FUSEDWASI_P2_FN void fusedwasi_p2_addr_text(const fusedwasi_p2_addr *a, char *out, size_t n) {
    if (a->family == 0) {
        inet_ntop(AF_INET, a->v4, out, (socklen_t)n);
    } else {
        struct in6_addr x;
        for (int i = 0; i < 8; i++) {
            x.s6_addr[2 * i] = (uint8_t)(a->v6[i] >> 8);
            x.s6_addr[2 * i + 1] = (uint8_t)a->v6[i];
        }
        inet_ntop(AF_INET6, &x, out, (socklen_t)n);
    }
}

FUSEDWASI_P2_FN socklen_t fusedwasi_p2_to_sockaddr(const fusedwasi_p2_addr *a, struct sockaddr_storage *ss) {
    memset(ss, 0, sizeof *ss);
    if (a->family == 0) {
        struct sockaddr_in *in = (struct sockaddr_in *)ss;
        in->sin_family = AF_INET;
        in->sin_port = htons(a->port);
        memcpy(&in->sin_addr, a->v4, 4);
        return sizeof *in;
    }
    struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ss;
    in6->sin6_family = AF_INET6;
    in6->sin6_port = htons(a->port);
    in6->sin6_flowinfo = htonl(a->flow);
    in6->sin6_scope_id = a->scope;
    for (int i = 0; i < 8; i++) {
        in6->sin6_addr.s6_addr[2 * i] = (uint8_t)(a->v6[i] >> 8);
        in6->sin6_addr.s6_addr[2 * i + 1] = (uint8_t)a->v6[i];
    }
    return sizeof *in6;
}

FUSEDWASI_P2_FN fusedwasi_p2_addr fusedwasi_p2_from_sockaddr(const struct sockaddr_storage *ss) {
    fusedwasi_p2_addr a;
    memset(&a, 0, sizeof a);
    if (ss->ss_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)ss;
        a.family = 0;
        a.port = ntohs(in->sin_port);
        memcpy(a.v4, &in->sin_addr, 4);
    } else {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)ss;
        a.family = 1;
        a.port = ntohs(in6->sin6_port);
        a.flow = ntohl(in6->sin6_flowinfo);
        a.scope = in6->sin6_scope_id;
        for (int i = 0; i < 8; i++) a.v6[i] = (uint16_t)(in6->sin6_addr.s6_addr[2 * i] << 8 | in6->sin6_addr.s6_addr[2 * i + 1]);
    }
    return a;
}

FUSEDWASI_P2_FN int fusedwasi_p2_addr_unspecified(const fusedwasi_p2_addr *a) {
    if (a->family == 0) return !(a->v4[0] | a->v4[1] | a->v4[2] | a->v4[3]);
    for (int i = 0; i < 8; i++)
        if (a->v6[i]) return 0;
    return 1;
}

/* a unicast address: not multicast, not the broadcast, not an IPv4-mapped IPv6 address (the unspecified address is allowed here) */
FUSEDWASI_P2_FN int fusedwasi_p2_addr_unicast(const fusedwasi_p2_addr *a) {
    if (a->family == 0) return !(a->v4[0] >= 224 && a->v4[0] <= 239) && !(a->v4[0] == 255 && a->v4[1] == 255 && a->v4[2] == 255 && a->v4[3] == 255);
    if ((a->v6[0] >> 8) == 0xff) return 0;
    if (!a->v6[0] && !a->v6[1] && !a->v6[2] && !a->v6[3] && !a->v6[4] && a->v6[5] == 0xffff) return 0;
    return 1;
}

/* the address in memory: the variant of 32 bytes at `ptr` */
FUSEDWASI_P2_FN void fusedwasi_p2_write_addr(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr, const fusedwasi_p2_addr *a) {
    fusedwasi_p2_store_u8(g, comp, ptr, (uint8_t)a->family);
    fusedwasi_p2_store_u16(g, comp, ptr + 4, a->port);
    if (a->family == 0) {
        memcpy(fusedwasi_p2_mem(g, comp, ptr + 6, 4), a->v4, 4);
    } else {
        fusedwasi_p2_store_u32(g, comp, ptr + 8, a->flow);
        for (int i = 0; i < 8; i++) fusedwasi_p2_store_u16(g, comp, ptr + 12 + 2 * i, a->v6[i]);
        fusedwasi_p2_store_u32(g, comp, ptr + 28, a->scope);
    }
}

FUSEDWASI_P2_FN fusedwasi_p2_addr fusedwasi_p2_read_addr(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr) {
    fusedwasi_p2_addr a;
    memset(&a, 0, sizeof a);
    a.family = fusedwasi_p2_load_u8(g, comp, ptr) == 0 ? 0 : 1;
    a.port = fusedwasi_p2_load_u16(g, comp, ptr + 4);
    if (a.family == 0) {
        memcpy(a.v4, fusedwasi_p2_mem(g, comp, ptr + 6, 4), 4);
    } else {
        a.flow = fusedwasi_p2_load_u32(g, comp, ptr + 8);
        for (int i = 0; i < 8; i++) a.v6[i] = fusedwasi_p2_load_u16(g, comp, ptr + 12 + 2 * i);
        a.scope = fusedwasi_p2_load_u32(g, comp, ptr + 28);
    }
    return a;
}

/* results */

FUSEDWASI_P2_FN void fusedwasi_p2_net_status(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, int rc) {
    if (rc != FUSEDWASI_P2_NOK) {
        fusedwasi_p2_store_u8(g, comp, retptr, 1);
        fusedwasi_p2_store_u8(g, comp, retptr + 1, (uint8_t)rc);
        return;
    }
    fusedwasi_p2_ok(g, comp, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_p2_net_fail(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, uint32_t at, int rc) {
    fusedwasi_p2_store_u8(g, comp, retptr, 1);
    fusedwasi_p2_store_u8(g, comp, at, (uint8_t)rc);
}

/* the objects */

typedef struct {
    int fd, family, state;
    uint32_t backlog;
    int recv_shut, send_shut;
    int refs; /* the handle of the socket and every stream of it */
} fusedwasi_p2_tcp;

typedef struct {
    int fd, family, state, closed;
    int has_remote;
    fusedwasi_p2_addr remote;
    int refs;
} fusedwasi_p2_udp;

typedef struct {
    fusedwasi_p2_udp *u;
    uint32_t permit; /* outgoing only */
} fusedwasi_p2_dgram_stream;

typedef struct {
    fusedwasi_p2_addr *list;
    uint32_t count, pos;
} fusedwasi_p2_resolved;

FUSEDWASI_P2_FN int fusedwasi_p2_set_nonblock(int fd) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &on) == 0 ? 0 : -1;
#else
    int fl = fcntl(fd, F_GETFL, 0);
    return fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0 ? -1 : 0;
#endif
}

FUSEDWASI_P2_FN int fusedwasi_p2_poll_fd(int fd, short events) {
    struct pollfd p = {fd, events, 0};
    return FUSEDWASI_P2_POLL(&p, 1, 0) > 0;
}

/* a socket of the given family and type: the descriptor, or -1 and the network error in *rc */
FUSEDWASI_P2_FN int fusedwasi_p2_net_socket(int family, int type, int *rc) {
    fusedwasi_p2_net_start();
    int fd = FUSEDWASI_P2_SOCKET(family == 0 ? AF_INET : AF_INET6, type);
    if (fd < 0) {
        *rc = fusedwasi_p2_net_errno(fusedwasi_p2_errno());
        return -1;
    }
    if (fusedwasi_p2_set_nonblock(fd) < 0) {
        *rc = fusedwasi_p2_net_errno(fusedwasi_p2_errno());
        FUSEDWASI_P2_CLOSE(fd);
        return -1;
    }
    int on = 1;
    if (family == 1) FUSEDWASI_P2_SETSOCKOPT(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof on);
    FUSEDWASI_P2_CLOEXEC(fd);
    return fd;
}

FUSEDWASI_P2_FN void fusedwasi_p2_tcp_unref(fusedwasi_p2_state *state, fusedwasi_p2_tcp *t) {
    (void)state;
    if (--t->refs == 0) free(t);
}

FUSEDWASI_P2_FN void fusedwasi_p2_udp_unref(fusedwasi_p2_state *state, fusedwasi_p2_udp *u) {
    (void)state;
    if (--u->refs == 0) free(u);
}

FUSEDWASI_P2_FN void fusedwasi_p2_tcp_destroy(fusedwasi_p2_state *state, void *obj) { /* the handle of the socket is dropped, or the state is freed */
    fusedwasi_p2_tcp *t = (fusedwasi_p2_tcp *)obj;
    if (t->fd >= 0) FUSEDWASI_P2_CLOSE(t->fd);
    t->fd = -1;
    t->state = FUSEDWASI_P2_SS_CLOSED;
    fusedwasi_p2_tcp_unref(state, t);
}

FUSEDWASI_P2_FN void fusedwasi_p2_udp_destroy(fusedwasi_p2_state *state, void *obj) {
    fusedwasi_p2_udp *u = (fusedwasi_p2_udp *)obj;
    if (u->fd >= 0) FUSEDWASI_P2_CLOSE(u->fd);
    u->fd = -1;
    u->closed = 1;
    fusedwasi_p2_udp_unref(state, u);
}

FUSEDWASI_P2_FN fusedwasi_p2_tcp *fusedwasi_p2_tcp_get(fusedwasi_p2_guest *g, uint32_t h) {
    return (fusedwasi_p2_tcp *)fusedwasi_p2_table_get(g, h, FUSEDWASI_P2_KIND_TCP_SOCKET)->obj;
}
FUSEDWASI_P2_FN fusedwasi_p2_udp *fusedwasi_p2_udp_get(fusedwasi_p2_guest *g, uint32_t h) {
    return (fusedwasi_p2_udp *)fusedwasi_p2_table_get(g, h, FUSEDWASI_P2_KIND_UDP_SOCKET)->obj;
}
FUSEDWASI_P2_FN void fusedwasi_p2_network_get(fusedwasi_p2_guest *g, uint32_t h) { fusedwasi_p2_table_get(g, h, FUSEDWASI_P2_KIND_NETWORK); }

/* policy and the checks of an address */
FUSEDWASI_P2_FN int fusedwasi_p2_policy(fusedwasi_p2_guest *g, const fusedwasi_p2_addr *a, int use) {
    char text[64];
    fusedwasi_p2_addr_text(a, text, sizeof text);
    return g->state->socket_addr_check && g->state->socket_addr_check(g->state, text, a->port, use) ? FUSEDWASI_P2_NOK : FUSEDWASI_P2_N_ACCESS_DENIED;
}

FUSEDWASI_P2_FN int fusedwasi_p2_check_bind(fusedwasi_p2_guest *g, int family, const fusedwasi_p2_addr *a, int use) {
    if (a->family != family || !fusedwasi_p2_addr_unicast(a)) return FUSEDWASI_P2_N_INVALID_ARGUMENT;
    return fusedwasi_p2_policy(g, a, use);
}

FUSEDWASI_P2_FN int fusedwasi_p2_check_connect(fusedwasi_p2_guest *g, int family, const fusedwasi_p2_addr *a, int use) {
    if (a->family != family || !fusedwasi_p2_addr_unicast(a) || fusedwasi_p2_addr_unspecified(a) || a->port == 0) return FUSEDWASI_P2_N_INVALID_ARGUMENT;
    return fusedwasi_p2_policy(g, a, use);
}

/* network */

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_network_network_drop(fusedwasi_p2_guest *g, uint32_t h) { fusedwasi_p2_table_drop(g, h, FUSEDWASI_P2_KIND_NETWORK); }

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_network_network_error_code(fusedwasi_p2_guest *g, uint32_t comp, uint32_t err, uint32_t retptr) {
    fusedwasi_p2_error *e = (fusedwasi_p2_error *)fusedwasi_p2_table_get(g, err, FUSEDWASI_P2_KIND_ERROR)->obj;
    if (e->network_code < 0) {
        fusedwasi_p2_store_u8(g, comp, retptr, 0);
        return;
    }
    fusedwasi_p2_store_u8(g, comp, retptr, 1);
    fusedwasi_p2_store_u8(g, comp, retptr + 1, (uint8_t)e->network_code);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_sockets_instance_network_instance_network(fusedwasi_p2_guest *g) {
    return fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_NETWORK, NULL, NULL);
}

/* the streams of a connection */

typedef struct {
    fusedwasi_p2_stream base;
    fusedwasi_p2_tcp *t;
} fusedwasi_p2_sock_stream;

FUSEDWASI_P2_FN void fusedwasi_p2_sock_stream_destroy(fusedwasi_p2_state *state, fusedwasi_p2_stream *st) {
    fusedwasi_p2_tcp_unref(state, ((fusedwasi_p2_sock_stream *)st)->t);
    fusedwasi_p2_stream_free(state, st);
}

FUSEDWASI_P2_FN void fusedwasi_p2_sock_fail(fusedwasi_p2_stream *st, int e, const char *what) {
    st->failure = what;
    st->failure_has_network = 1;
    st->failure_network_code = fusedwasi_p2_net_errno(e);
}

FUSEDWASI_P2_FN int fusedwasi_p2_sock_read(fusedwasi_p2_guest *g, fusedwasi_p2_stream *st, uint8_t *buf, uint64_t len, uint64_t *got) {
    fusedwasi_p2_tcp *t = ((fusedwasi_p2_sock_stream *)st)->t;
    (void)g;
    *got = 0;
    if (t->recv_shut || t->state == FUSEDWASI_P2_SS_CLOSED) return FUSEDWASI_P2_STREAM_CLOSED;
    if (len == 0) return FUSEDWASI_P2_STREAM_OK;
    fusedwasi_p2_ssize n = FUSEDWASI_P2_RECV(t->fd, buf, (size_t)len, 0);
    if (n < 0) {
        if (fusedwasi_p2_errno() == EAGAIN || fusedwasi_p2_errno() == EWOULDBLOCK) return FUSEDWASI_P2_STREAM_OK;
        fusedwasi_p2_sock_fail(st, fusedwasi_p2_errno(), "read failed");
        return FUSEDWASI_P2_STREAM_FAILED;
    }
    if (n == 0) return FUSEDWASI_P2_STREAM_CLOSED;
    *got = (uint64_t)n;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sock_in_ready(fusedwasi_p2_guest *g, fusedwasi_p2_stream *st) {
    fusedwasi_p2_tcp *t = ((fusedwasi_p2_sock_stream *)st)->t;
    (void)g;
    return t->recv_shut || t->state == FUSEDWASI_P2_SS_CLOSED || fusedwasi_p2_poll_fd(t->fd, POLLIN);
}

FUSEDWASI_P2_FN uint64_t fusedwasi_p2_sock_check_write(fusedwasi_p2_guest *g, fusedwasi_p2_stream *st, int *status) {
    fusedwasi_p2_tcp *t = ((fusedwasi_p2_sock_stream *)st)->t;
    (void)g;
    *status = t->send_shut || t->state == FUSEDWASI_P2_SS_CLOSED ? FUSEDWASI_P2_STREAM_CLOSED : FUSEDWASI_P2_STREAM_OK;
    return *status == FUSEDWASI_P2_STREAM_OK ? FUSEDWASI_P2_SOCK_CHUNK : 0;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sock_write(fusedwasi_p2_guest *g, fusedwasi_p2_stream *st, const uint8_t *data, uint64_t len) {
    fusedwasi_p2_tcp *t = ((fusedwasi_p2_sock_stream *)st)->t;
    (void)g;
    while (len) {
        fusedwasi_p2_ssize n = FUSEDWASI_P2_SEND(t->fd, data, (size_t)len, FUSEDWASI_P2_MSG_NOSIGNAL);
        if (n < 0) {
            if (fusedwasi_p2_errno() == EAGAIN || fusedwasi_p2_errno() == EWOULDBLOCK) { /* the system buffer is full: wait for room */
                struct pollfd p = {t->fd, POLLOUT, 0};
                FUSEDWASI_P2_POLL(&p, 1, 50);
                continue;
            }
            if (fusedwasi_p2_errno() == EINTR) continue;
            fusedwasi_p2_sock_fail(st, fusedwasi_p2_errno(), "write failed");
            return FUSEDWASI_P2_STREAM_FAILED;
        }
        data += n;
        len -= (uint64_t)n;
    }
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sock_flush(fusedwasi_p2_guest *g, fusedwasi_p2_stream *st) {
    (void)g, (void)st;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sock_out_ready(fusedwasi_p2_guest *g, fusedwasi_p2_stream *st) {
    fusedwasi_p2_tcp *t = ((fusedwasi_p2_sock_stream *)st)->t;
    (void)g;
    return t->send_shut || t->state == FUSEDWASI_P2_SS_CLOSED || fusedwasi_p2_poll_fd(t->fd, POLLOUT);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_sock_stream_new(fusedwasi_p2_guest *g, fusedwasi_p2_tcp *t, int output) {
    fusedwasi_p2_sock_stream *st = (fusedwasi_p2_sock_stream *)calloc(1, sizeof *st);
    if (!st) g->trap(g, "out of memory");
    st->t = t;
    t->refs++;
    st->base.destroy = fusedwasi_p2_sock_stream_destroy;
    if (output) {
        st->base.check_write = fusedwasi_p2_sock_check_write;
        st->base.write = fusedwasi_p2_sock_write;
        st->base.flush = fusedwasi_p2_sock_flush;
        st->base.ready = fusedwasi_p2_sock_out_ready;
        return fusedwasi_p2_output_stream_new(g, &st->base);
    }
    st->base.read = fusedwasi_p2_sock_read;
    st->base.ready = fusedwasi_p2_sock_in_ready;
    return fusedwasi_p2_input_stream_new(g, &st->base);
}

/* tcp-create-socket */

FUSEDWASI_P2_FN fusedwasi_p2_tcp *fusedwasi_p2_tcp_make(fusedwasi_p2_guest *g, int family, int fd, int state) {
    fusedwasi_p2_tcp *t = (fusedwasi_p2_tcp *)calloc(1, sizeof *t);
    if (!t) g->trap(g, "out of memory");
    t->fd = fd, t->family = family, t->state = state, t->backlog = 128, t->refs = 1;
    return t;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_create_socket_create_tcp_socket(fusedwasi_p2_guest *g, uint32_t comp, uint32_t address_family, uint32_t retptr) {
    int rc = FUSEDWASI_P2_NOK;
    int fd = fusedwasi_p2_net_socket((int)address_family, SOCK_STREAM, &rc);
    if (fd < 0) {
        fusedwasi_p2_net_fail(g, comp, retptr, retptr + 4, rc);
        return;
    }
    fusedwasi_p2_ok(g, comp, retptr);
    fusedwasi_p2_store_u32(g, comp, retptr + 4,
                      fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_TCP_SOCKET, fusedwasi_p2_tcp_make(g, (int)address_family, fd, FUSEDWASI_P2_SS_UNBOUND), fusedwasi_p2_tcp_destroy));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_drop(fusedwasi_p2_guest *g, uint32_t h) {
    fusedwasi_p2_tcp_get(g, h);
    fusedwasi_p2_table_drop(g, h, FUSEDWASI_P2_KIND_TCP_SOCKET);
}

FUSEDWASI_P2_FN int fusedwasi_p2_tcp_open(const fusedwasi_p2_tcp *t) { return t->state == FUSEDWASI_P2_SS_CLOSED ? FUSEDWASI_P2_N_INVALID_STATE : FUSEDWASI_P2_NOK; }

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_start_bind(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t network, uint32_t tag, uint32_t p1, uint32_t p2,
                                                       uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8, uint32_t p9, uint32_t p10,
                                                       uint32_t p11, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    fusedwasi_p2_network_get(g, network);
    uint32_t f[11] = {p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11};
    fusedwasi_p2_addr a = fusedwasi_p2_addr_flat(tag, f);
    int rc = FUSEDWASI_P2_NOK;
    if (t->state != FUSEDWASI_P2_SS_UNBOUND) rc = FUSEDWASI_P2_N_INVALID_STATE;
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_check_bind(g, t->family, &a, FUSEDWASI_P2_USE_TCP_BIND);
    if (rc == FUSEDWASI_P2_NOK) {
#ifdef _WIN32
        /* SO_REUSEADDR on Windows lets another program take the port; the address is made exclusive instead */
        int on = 1;
        FUSEDWASI_P2_SETSOCKOPT(t->fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, &on, sizeof on);
#else
        int on = 1;
        FUSEDWASI_P2_SETSOCKOPT(t->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
#endif
        struct sockaddr_storage ss;
        socklen_t n = fusedwasi_p2_to_sockaddr(&a, &ss);
        if (bind(t->fd, (struct sockaddr *)&ss, n) < 0) rc = fusedwasi_p2_net_errno(fusedwasi_p2_errno());
        else t->state = FUSEDWASI_P2_SS_BIND_IN_PROGRESS;
    }
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_finish_bind(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = FUSEDWASI_P2_NOK;
    if (t->state != FUSEDWASI_P2_SS_BIND_IN_PROGRESS) rc = FUSEDWASI_P2_N_NOT_IN_PROGRESS;
    else t->state = FUSEDWASI_P2_SS_BOUND;
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_start_connect(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t network, uint32_t tag, uint32_t p1,
                                                          uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8, uint32_t p9,
                                                          uint32_t p10, uint32_t p11, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    fusedwasi_p2_network_get(g, network);
    uint32_t f[11] = {p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11};
    fusedwasi_p2_addr a = fusedwasi_p2_addr_flat(tag, f);
    int rc = FUSEDWASI_P2_NOK;
    if (t->state != FUSEDWASI_P2_SS_UNBOUND && t->state != FUSEDWASI_P2_SS_BOUND) rc = FUSEDWASI_P2_N_INVALID_STATE;
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_check_connect(g, t->family, &a, FUSEDWASI_P2_USE_TCP_CONNECT);
    if (rc == FUSEDWASI_P2_NOK) {
        struct sockaddr_storage ss;
        socklen_t n = fusedwasi_p2_to_sockaddr(&a, &ss);
        if (connect(t->fd, (struct sockaddr *)&ss, n) < 0 && fusedwasi_p2_errno() != EINPROGRESS && fusedwasi_p2_errno() != EWOULDBLOCK) {
            rc = fusedwasi_p2_net_errno(fusedwasi_p2_errno());
            t->state = FUSEDWASI_P2_SS_CLOSED;
        } else {
            t->state = FUSEDWASI_P2_SS_CONNECT_IN_PROGRESS;
        }
    }
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_finish_connect(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = FUSEDWASI_P2_NOK;
    if (t->state != FUSEDWASI_P2_SS_CONNECT_IN_PROGRESS) {
        rc = FUSEDWASI_P2_N_NOT_IN_PROGRESS;
    } else if (!fusedwasi_p2_poll_fd(t->fd, POLLOUT)) {
        rc = FUSEDWASI_P2_N_WOULD_BLOCK;
    } else {
        int err = 0;
        socklen_t n = sizeof err;
        FUSEDWASI_P2_GETSOCKOPT(t->fd, SOL_SOCKET, SO_ERROR, &err, &n);
        if (err) {
            t->state = FUSEDWASI_P2_SS_CLOSED;
            rc = fusedwasi_p2_net_errno(fusedwasi_p2_sockerr(err));
        } else {
            t->state = FUSEDWASI_P2_SS_CONNECTED;
        }
    }
    if (rc != FUSEDWASI_P2_NOK) {
        fusedwasi_p2_net_fail(g, comp, retptr, retptr + 4, rc);
        return;
    }
    fusedwasi_p2_ok(g, comp, retptr);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, fusedwasi_p2_sock_stream_new(g, t, 0));
    fusedwasi_p2_store_u32(g, comp, retptr + 8, fusedwasi_p2_sock_stream_new(g, t, 1));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_start_listen(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = FUSEDWASI_P2_NOK;
    if (t->state != FUSEDWASI_P2_SS_BOUND) rc = FUSEDWASI_P2_N_INVALID_STATE;
    else if (listen(t->fd, (int)t->backlog) < 0) rc = fusedwasi_p2_net_errno(fusedwasi_p2_errno());
    else t->state = FUSEDWASI_P2_SS_LISTEN_IN_PROGRESS;
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_finish_listen(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = FUSEDWASI_P2_NOK;
    if (t->state != FUSEDWASI_P2_SS_LISTEN_IN_PROGRESS) rc = FUSEDWASI_P2_N_NOT_IN_PROGRESS;
    else t->state = FUSEDWASI_P2_SS_LISTENING;
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_accept(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = FUSEDWASI_P2_NOK, fd = -1;
    if (t->state != FUSEDWASI_P2_SS_LISTENING) {
        rc = FUSEDWASI_P2_N_INVALID_STATE;
    } else {
        fd = FUSEDWASI_P2_ACCEPT(t->fd);
        if (fd < 0) rc = fusedwasi_p2_errno() == EAGAIN || fusedwasi_p2_errno() == EWOULDBLOCK ? FUSEDWASI_P2_N_WOULD_BLOCK : fusedwasi_p2_net_errno(fusedwasi_p2_errno());
        else if (fusedwasi_p2_set_nonblock(fd) < 0) rc = fusedwasi_p2_net_errno(fusedwasi_p2_errno());
    }
    if (rc != FUSEDWASI_P2_NOK) {
        if (fd >= 0) FUSEDWASI_P2_CLOSE(fd);
        fusedwasi_p2_net_fail(g, comp, retptr, retptr + 4, rc);
        return;
    }
    FUSEDWASI_P2_CLOEXEC(fd);
    fusedwasi_p2_tcp *conn = fusedwasi_p2_tcp_make(g, t->family, fd, FUSEDWASI_P2_SS_CONNECTED);
    fusedwasi_p2_ok(g, comp, retptr);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_TCP_SOCKET, conn, fusedwasi_p2_tcp_destroy));
    fusedwasi_p2_store_u32(g, comp, retptr + 8, fusedwasi_p2_sock_stream_new(g, conn, 0));
    fusedwasi_p2_store_u32(g, comp, retptr + 12, fusedwasi_p2_sock_stream_new(g, conn, 1));
}

FUSEDWASI_P2_FN void fusedwasi_p2_address_result(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, int rc, const fusedwasi_p2_addr *a) {
    if (rc != FUSEDWASI_P2_NOK) {
        fusedwasi_p2_net_fail(g, comp, retptr, retptr + 4, rc);
        return;
    }
    fusedwasi_p2_ok(g, comp, retptr);
    fusedwasi_p2_write_addr(g, comp, retptr + 4, a);
}

FUSEDWASI_P2_FN int fusedwasi_p2_name_of(int fd, int peer, fusedwasi_p2_addr *out) {
    struct sockaddr_storage ss;
    socklen_t n = sizeof ss;
    if ((peer ? getpeername(fd, (struct sockaddr *)&ss, &n) : getsockname(fd, (struct sockaddr *)&ss, &n)) < 0) return fusedwasi_p2_net_errno(fusedwasi_p2_errno());
    *out = fusedwasi_p2_from_sockaddr(&ss);
    return FUSEDWASI_P2_NOK;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_local_address(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    fusedwasi_p2_addr a;
    memset(&a, 0, sizeof a);
    int rc = t->state == FUSEDWASI_P2_SS_UNBOUND || t->state == FUSEDWASI_P2_SS_BIND_IN_PROGRESS || t->state == FUSEDWASI_P2_SS_CLOSED ? FUSEDWASI_P2_N_INVALID_STATE
                                                                                                                         : fusedwasi_p2_name_of(t->fd, 0, &a);
    fusedwasi_p2_address_result(g, comp, retptr, rc, &a);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_remote_address(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    fusedwasi_p2_addr a;
    memset(&a, 0, sizeof a);
    int rc = t->state != FUSEDWASI_P2_SS_CONNECTED ? FUSEDWASI_P2_N_INVALID_STATE : fusedwasi_p2_name_of(t->fd, 1, &a);
    fusedwasi_p2_address_result(g, comp, retptr, rc, &a);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_sockets_tcp_tcp_socket_is_listening(fusedwasi_p2_guest *g, uint32_t self) { return fusedwasi_p2_tcp_get(g, self)->state == FUSEDWASI_P2_SS_LISTENING; }

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_sockets_tcp_tcp_socket_address_family(fusedwasi_p2_guest *g, uint32_t self) { return (uint32_t)fusedwasi_p2_tcp_get(g, self)->family; }

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_set_listen_backlog_size(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t value, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = fusedwasi_p2_tcp_open(t);
    if (rc == FUSEDWASI_P2_NOK && value == 0) rc = FUSEDWASI_P2_N_INVALID_ARGUMENT;
    if (rc == FUSEDWASI_P2_NOK && (t->state == FUSEDWASI_P2_SS_CONNECT_IN_PROGRESS || t->state == FUSEDWASI_P2_SS_CONNECTED)) rc = FUSEDWASI_P2_N_INVALID_STATE;
    if (rc == FUSEDWASI_P2_NOK) {
        t->backlog = value < FUSEDWASI_P2_BACKLOG_MAX ? (uint32_t)value : FUSEDWASI_P2_BACKLOG_MAX;
        if (t->state == FUSEDWASI_P2_SS_LISTENING && listen(t->fd, (int)t->backlog) < 0) rc = fusedwasi_p2_net_errno(fusedwasi_p2_errno());
    }
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

/* options: numbers from getsockopt / to setsockopt; the names that a system lacks are `not-supported` */

FUSEDWASI_P2_FN int fusedwasi_p2_getopt(int fd, int level, int name, int *value) {
    socklen_t n = sizeof *value;
    return FUSEDWASI_P2_GETSOCKOPT(fd, level, name, value, &n) < 0 ? fusedwasi_p2_net_errno(fusedwasi_p2_errno()) : FUSEDWASI_P2_NOK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_setopt(int fd, int level, int name, int value) {
    return FUSEDWASI_P2_SETSOCKOPT(fd, level, name, &value, sizeof value) < 0 ? fusedwasi_p2_net_errno(fusedwasi_p2_errno()) : FUSEDWASI_P2_NOK;
}

#ifdef TCP_KEEPIDLE
#define FUSEDWASI_P2_KEEPIDLE TCP_KEEPIDLE
#elif defined(TCP_KEEPALIVE)
#define FUSEDWASI_P2_KEEPIDLE TCP_KEEPALIVE
#endif

FUSEDWASI_P2_FN int fusedwasi_p2_seconds(uint64_t ns) {
    uint64_t sec = (ns + 999999999ULL) / 1000000000ULL;
    return sec < 1 ? 1 : sec > 2147483647ULL ? 2147483647 : (int)sec;
}

/* result<number>: the value stored at `at` with `size` bytes (1, 4 or 8) */
FUSEDWASI_P2_FN void fusedwasi_p2_number_result(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, uint32_t at, int size, int rc, uint64_t value) {
    if (rc != FUSEDWASI_P2_NOK) {
        fusedwasi_p2_net_fail(g, comp, retptr, retptr + at, rc);
        return;
    }
    fusedwasi_p2_ok(g, comp, retptr);
    if (size == 1) fusedwasi_p2_store_u8(g, comp, retptr + at, (uint8_t)value);
    else if (size == 4) fusedwasi_p2_store_u32(g, comp, retptr + at, (uint32_t)value);
    else fusedwasi_p2_store_u64(g, comp, retptr + at, value);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_keep_alive_enabled(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int v = 0, rc = fusedwasi_p2_tcp_open(t);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_getopt(t->fd, SOL_SOCKET, SO_KEEPALIVE, &v);
    fusedwasi_p2_number_result(g, comp, retptr, 1, 1, rc, v != 0);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_set_keep_alive_enabled(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t value, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = fusedwasi_p2_tcp_open(t);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_setopt(t->fd, SOL_SOCKET, SO_KEEPALIVE, value != 0);
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

/* `name` < 0: the system has no such option */
FUSEDWASI_P2_FN void fusedwasi_p2_keepalive_get(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr, int name, uint64_t factor, int size) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int v = 0, rc = fusedwasi_p2_tcp_open(t);
    if (rc == FUSEDWASI_P2_NOK && name < 0) rc = FUSEDWASI_P2_N_NOT_SUPPORTED;
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_getopt(t->fd, IPPROTO_TCP, name, &v);
    fusedwasi_p2_number_result(g, comp, retptr, size == 8 ? 8 : 4, size, rc, (uint64_t)v * factor);
}

FUSEDWASI_P2_FN void fusedwasi_p2_keepalive_set(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr, int name, uint64_t value, int seconds) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = fusedwasi_p2_tcp_open(t);
    if (rc == FUSEDWASI_P2_NOK && value == 0) rc = FUSEDWASI_P2_N_INVALID_ARGUMENT;
    if (rc == FUSEDWASI_P2_NOK && name < 0) rc = FUSEDWASI_P2_N_NOT_SUPPORTED;
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_setopt(t->fd, IPPROTO_TCP, name, seconds ? fusedwasi_p2_seconds(value) : (int)(value < 2147483647ULL ? value : 2147483647ULL));
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

#ifdef FUSEDWASI_P2_KEEPIDLE
#define FUSEDWASI_P2_OPT_IDLE FUSEDWASI_P2_KEEPIDLE
#else
#define FUSEDWASI_P2_OPT_IDLE (-1)
#endif
#ifdef TCP_KEEPINTVL
#define FUSEDWASI_P2_OPT_INTVL TCP_KEEPINTVL
#else
#define FUSEDWASI_P2_OPT_INTVL (-1)
#endif
#ifdef TCP_KEEPCNT
#define FUSEDWASI_P2_OPT_CNT TCP_KEEPCNT
#else
#define FUSEDWASI_P2_OPT_CNT (-1)
#endif

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_keep_alive_idle_time(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_keepalive_get(g, comp, self, retptr, FUSEDWASI_P2_OPT_IDLE, 1000000000ULL, 8);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_set_keep_alive_idle_time(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t value, uint32_t retptr) {
    fusedwasi_p2_keepalive_set(g, comp, self, retptr, FUSEDWASI_P2_OPT_IDLE, value, 1);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_keep_alive_interval(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_keepalive_get(g, comp, self, retptr, FUSEDWASI_P2_OPT_INTVL, 1000000000ULL, 8);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_set_keep_alive_interval(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t value, uint32_t retptr) {
    fusedwasi_p2_keepalive_set(g, comp, self, retptr, FUSEDWASI_P2_OPT_INTVL, value, 1);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_keep_alive_count(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_keepalive_get(g, comp, self, retptr, FUSEDWASI_P2_OPT_CNT, 1, 4);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_set_keep_alive_count(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t value, uint32_t retptr) {
    fusedwasi_p2_keepalive_set(g, comp, self, retptr, FUSEDWASI_P2_OPT_CNT, value, 0);
}

FUSEDWASI_P2_FN int fusedwasi_p2_hop_get(int fd, int family, int *v) {
    int rc = family == 0 ? fusedwasi_p2_getopt(fd, IPPROTO_IP, IP_TTL, v) : fusedwasi_p2_getopt(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, v);
    if (rc == FUSEDWASI_P2_NOK && *v > 255) *v = 255;
    return rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2_hop_set(int fd, int family, uint32_t value) {
    if ((value & 0xFF) == 0) return FUSEDWASI_P2_N_INVALID_ARGUMENT;
    return family == 0 ? fusedwasi_p2_setopt(fd, IPPROTO_IP, IP_TTL, (int)(value & 0xFF)) : fusedwasi_p2_setopt(fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (int)(value & 0xFF));
}

FUSEDWASI_P2_FN int fusedwasi_p2_buffer_set(int fd, int name, uint64_t value) {
    if (value == 0) return FUSEDWASI_P2_N_INVALID_ARGUMENT;
    return fusedwasi_p2_setopt(fd, SOL_SOCKET, name, (int)(value < 2147483647ULL ? value : 2147483647ULL));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_hop_limit(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int v = 0, rc = fusedwasi_p2_tcp_open(t);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_hop_get(t->fd, t->family, &v);
    fusedwasi_p2_number_result(g, comp, retptr, 1, 1, rc, (uint64_t)v);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_set_hop_limit(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t value, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = fusedwasi_p2_tcp_open(t);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_hop_set(t->fd, t->family, value);
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_receive_buffer_size(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int v = 0, rc = fusedwasi_p2_tcp_open(t);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_getopt(t->fd, SOL_SOCKET, SO_RCVBUF, &v);
    fusedwasi_p2_number_result(g, comp, retptr, 8, 8, rc, (uint64_t)v);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_set_receive_buffer_size(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t value, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = fusedwasi_p2_tcp_open(t);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_buffer_set(t->fd, SO_RCVBUF, value);
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_send_buffer_size(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int v = 0, rc = fusedwasi_p2_tcp_open(t);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_getopt(t->fd, SOL_SOCKET, SO_SNDBUF, &v);
    fusedwasi_p2_number_result(g, comp, retptr, 8, 8, rc, (uint64_t)v);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_set_send_buffer_size(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t value, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = fusedwasi_p2_tcp_open(t);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_buffer_set(t->fd, SO_SNDBUF, value);
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

/* a pollable that asks a socket */
typedef struct {
    int kind; /* 0 tcp, 1 udp readable, 2 udp writable, 3 udp either, 4 always */
    fusedwasi_p2_tcp *t;
    fusedwasi_p2_udp *u;
} fusedwasi_p2_sock_poll;

FUSEDWASI_P2_FN int fusedwasi_p2_sock_poll_ready(fusedwasi_p2_guest *g, fusedwasi_p2_pollable *p) {
    fusedwasi_p2_sock_poll *sp = (fusedwasi_p2_sock_poll *)p->ctx;
    (void)g;
    switch (sp->kind) {
    case 0:
        if (sp->t->state == FUSEDWASI_P2_SS_CONNECT_IN_PROGRESS) return fusedwasi_p2_poll_fd(sp->t->fd, POLLOUT);
        if (sp->t->state == FUSEDWASI_P2_SS_LISTENING) return fusedwasi_p2_poll_fd(sp->t->fd, POLLIN);
        return 1;
    case 1: return fusedwasi_p2_poll_fd(sp->u->fd, POLLIN);
    case 2: return fusedwasi_p2_poll_fd(sp->u->fd, POLLOUT);
    case 3: return fusedwasi_p2_poll_fd(sp->u->fd, POLLIN) || fusedwasi_p2_poll_fd(sp->u->fd, POLLOUT);
    default: return 1;
    }
}

/* the pollable owns a reference to the socket (so that it can ask it), released when the pollable is dropped */
FUSEDWASI_P2_FN void fusedwasi_p2_sock_poll_destroy(fusedwasi_p2_state *state, void *obj) {
    fusedwasi_p2_pollable *p = (fusedwasi_p2_pollable *)obj;
    fusedwasi_p2_sock_poll *sp = (fusedwasi_p2_sock_poll *)p->ctx;
    if (sp->t) fusedwasi_p2_tcp_unref(state, sp->t);
    if (sp->u) fusedwasi_p2_udp_unref(state, sp->u);
    free(sp);
    free(p);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_sock_pollable(fusedwasi_p2_guest *g, int kind, fusedwasi_p2_tcp *t, fusedwasi_p2_udp *u) {
    fusedwasi_p2_pollable *p = (fusedwasi_p2_pollable *)calloc(1, sizeof *p);
    fusedwasi_p2_sock_poll *sp = (fusedwasi_p2_sock_poll *)calloc(1, sizeof *sp);
    if (!p || !sp) g->trap(g, "out of memory");
    sp->kind = kind, sp->t = t, sp->u = u;
    if (t) t->refs++;
    if (u) u->refs++;
    p->ready = fusedwasi_p2_sock_poll_ready;
    p->ctx = sp;
    return fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_POLLABLE, p, fusedwasi_p2_sock_poll_destroy);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_sockets_tcp_tcp_socket_subscribe(fusedwasi_p2_guest *g, uint32_t self) {
    return fusedwasi_p2_sock_pollable(g, 0, fusedwasi_p2_tcp_get(g, self), NULL);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_tcp_tcp_socket_shutdown(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t shutdown_type, uint32_t retptr) {
    fusedwasi_p2_tcp *t = fusedwasi_p2_tcp_get(g, self);
    int rc = FUSEDWASI_P2_NOK;
    if (t->state != FUSEDWASI_P2_SS_CONNECTED) {
        rc = FUSEDWASI_P2_N_INVALID_STATE;
    } else {
        int how = shutdown_type == 0 ? FUSEDWASI_P2_SHUT_RD : shutdown_type == 1 ? FUSEDWASI_P2_SHUT_WR : FUSEDWASI_P2_SHUT_RDWR;
        if (shutdown(t->fd, how) < 0 && fusedwasi_p2_errno() != ENOTCONN) rc = fusedwasi_p2_net_errno(fusedwasi_p2_errno());
        else {
            if (how != FUSEDWASI_P2_SHUT_WR) t->recv_shut = 1;
            if (how != FUSEDWASI_P2_SHUT_RD) t->send_shut = 1;
        }
    }
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

/* udp-create-socket and udp */

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_create_socket_create_udp_socket(fusedwasi_p2_guest *g, uint32_t comp, uint32_t address_family, uint32_t retptr) {
    int rc = FUSEDWASI_P2_NOK;
    int fd = fusedwasi_p2_net_socket((int)address_family, SOCK_DGRAM, &rc);
    if (fd < 0) {
        fusedwasi_p2_net_fail(g, comp, retptr, retptr + 4, rc);
        return;
    }
#ifdef _WIN32
    { /* a datagram sent to a closed port would make the next receive fail with a reset; no other system does that */
#ifndef FUSEDWASI_P2_SIO_UDP_CONNRESET
#define FUSEDWASI_P2_SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
        BOOL off = FALSE;
        DWORD ret = 0;
        WSAIoctl((SOCKET)fd, FUSEDWASI_P2_SIO_UDP_CONNRESET, &off, sizeof off, NULL, 0, &ret, NULL, NULL);
    }
#endif
    fusedwasi_p2_udp *u = (fusedwasi_p2_udp *)calloc(1, sizeof *u);
    if (!u) g->trap(g, "out of memory");
    u->fd = fd, u->family = (int)address_family, u->state = FUSEDWASI_P2_SS_UNBOUND, u->refs = 1;
    fusedwasi_p2_ok(g, comp, retptr);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_UDP_SOCKET, u, fusedwasi_p2_udp_destroy));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_drop(fusedwasi_p2_guest *g, uint32_t h) {
    fusedwasi_p2_udp_get(g, h);
    fusedwasi_p2_table_drop(g, h, FUSEDWASI_P2_KIND_UDP_SOCKET);
}

FUSEDWASI_P2_FN int fusedwasi_p2_udp_open(const fusedwasi_p2_udp *u) { return u->closed ? FUSEDWASI_P2_N_INVALID_STATE : FUSEDWASI_P2_NOK; }

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_start_bind(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t network, uint32_t tag, uint32_t p1, uint32_t p2,
                                                       uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8, uint32_t p9, uint32_t p10,
                                                       uint32_t p11, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_udp_get(g, self);
    fusedwasi_p2_network_get(g, network);
    uint32_t f[11] = {p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11};
    fusedwasi_p2_addr a = fusedwasi_p2_addr_flat(tag, f);
    int rc = FUSEDWASI_P2_NOK;
    if (u->state != FUSEDWASI_P2_SS_UNBOUND) rc = FUSEDWASI_P2_N_INVALID_STATE;
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_check_bind(g, u->family, &a, FUSEDWASI_P2_USE_UDP_BIND);
    if (rc == FUSEDWASI_P2_NOK) {
        struct sockaddr_storage ss;
        socklen_t n = fusedwasi_p2_to_sockaddr(&a, &ss);
        if (bind(u->fd, (struct sockaddr *)&ss, n) < 0) rc = fusedwasi_p2_net_errno(fusedwasi_p2_errno());
        else u->state = FUSEDWASI_P2_SS_BIND_IN_PROGRESS;
    }
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_finish_bind(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_udp_get(g, self);
    int rc = FUSEDWASI_P2_NOK;
    if (u->state != FUSEDWASI_P2_SS_BIND_IN_PROGRESS) rc = FUSEDWASI_P2_N_NOT_IN_PROGRESS;
    else u->state = FUSEDWASI_P2_SS_BOUND;
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

FUSEDWASI_P2_FN void fusedwasi_p2_dgram_destroy(fusedwasi_p2_state *state, void *obj) { /* the handle is dropped, or the state is freed */
    fusedwasi_p2_dgram_stream *d = (fusedwasi_p2_dgram_stream *)obj;
    fusedwasi_p2_udp_unref(state, d->u);
    free(d);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_dgram_stream_new(fusedwasi_p2_guest *g, fusedwasi_p2_udp *u, uint32_t kind) {
    fusedwasi_p2_dgram_stream *d = (fusedwasi_p2_dgram_stream *)calloc(1, sizeof *d);
    if (!d) g->trap(g, "out of memory");
    d->u = u;
    u->refs++;
    return fusedwasi_p2_table_new(g, kind, d, fusedwasi_p2_dgram_destroy);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_stream(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t remote_tag, uint32_t tag, uint32_t p1, uint32_t p2,
                                                   uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8, uint32_t p9, uint32_t p10,
                                                   uint32_t p11, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_udp_get(g, self);
    uint32_t f[11] = {p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11};
    fusedwasi_p2_addr a = fusedwasi_p2_addr_flat(tag, f);
    int rc = fusedwasi_p2_udp_open(u);
    if (rc == FUSEDWASI_P2_NOK && u->state != FUSEDWASI_P2_SS_BOUND) rc = FUSEDWASI_P2_N_INVALID_STATE;
    if (rc == FUSEDWASI_P2_NOK && remote_tag) rc = fusedwasi_p2_check_connect(g, u->family, &a, FUSEDWASI_P2_USE_UDP_CONNECT);
    if (rc != FUSEDWASI_P2_NOK) {
        fusedwasi_p2_net_fail(g, comp, retptr, retptr + 4, rc);
        return;
    }
    u->has_remote = remote_tag != 0;
    if (u->has_remote) u->remote = a;
    fusedwasi_p2_ok(g, comp, retptr);
    /* the handles of the two streams; their kinds free them with a reference to the socket */
    uint32_t in = fusedwasi_p2_dgram_stream_new(g, u, FUSEDWASI_P2_KIND_INCOMING_DATAGRAM_STREAM);
    uint32_t out = fusedwasi_p2_dgram_stream_new(g, u, FUSEDWASI_P2_KIND_OUTGOING_DATAGRAM_STREAM);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, in);
    fusedwasi_p2_store_u32(g, comp, retptr + 8, out);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_local_address(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_udp_get(g, self);
    fusedwasi_p2_addr a;
    memset(&a, 0, sizeof a);
    int rc = u->state != FUSEDWASI_P2_SS_BOUND || u->closed ? FUSEDWASI_P2_N_INVALID_STATE : fusedwasi_p2_name_of(u->fd, 0, &a);
    fusedwasi_p2_address_result(g, comp, retptr, rc, &a);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_remote_address(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_udp_get(g, self);
    int rc = !u->has_remote || u->closed ? FUSEDWASI_P2_N_INVALID_STATE : FUSEDWASI_P2_NOK;
    fusedwasi_p2_address_result(g, comp, retptr, rc, &u->remote);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_sockets_udp_udp_socket_address_family(fusedwasi_p2_guest *g, uint32_t self) { return (uint32_t)fusedwasi_p2_udp_get(g, self)->family; }

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_unicast_hop_limit(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_udp_get(g, self);
    int v = 0, rc = fusedwasi_p2_udp_open(u);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_hop_get(u->fd, u->family, &v);
    fusedwasi_p2_number_result(g, comp, retptr, 1, 1, rc, (uint64_t)v);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_set_unicast_hop_limit(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t value, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_udp_get(g, self);
    int rc = fusedwasi_p2_udp_open(u);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_hop_set(u->fd, u->family, value);
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_receive_buffer_size(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_udp_get(g, self);
    int v = 0, rc = fusedwasi_p2_udp_open(u);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_getopt(u->fd, SOL_SOCKET, SO_RCVBUF, &v);
    fusedwasi_p2_number_result(g, comp, retptr, 8, 8, rc, (uint64_t)v);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_set_receive_buffer_size(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t value, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_udp_get(g, self);
    int rc = fusedwasi_p2_udp_open(u);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_buffer_set(u->fd, SO_RCVBUF, value);
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_send_buffer_size(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_udp_get(g, self);
    int v = 0, rc = fusedwasi_p2_udp_open(u);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_getopt(u->fd, SOL_SOCKET, SO_SNDBUF, &v);
    fusedwasi_p2_number_result(g, comp, retptr, 8, 8, rc, (uint64_t)v);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_udp_socket_set_send_buffer_size(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t value, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_udp_get(g, self);
    int rc = fusedwasi_p2_udp_open(u);
    if (rc == FUSEDWASI_P2_NOK) rc = fusedwasi_p2_buffer_set(u->fd, SO_SNDBUF, value);
    fusedwasi_p2_net_status(g, comp, retptr, rc);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_sockets_udp_udp_socket_subscribe(fusedwasi_p2_guest *g, uint32_t self) {
    return fusedwasi_p2_sock_pollable(g, 3, NULL, fusedwasi_p2_udp_get(g, self));
}

FUSEDWASI_P2_FN fusedwasi_p2_dgram_stream *fusedwasi_p2_incoming_get(fusedwasi_p2_guest *g, uint32_t h) {
    return (fusedwasi_p2_dgram_stream *)fusedwasi_p2_table_get(g, h, FUSEDWASI_P2_KIND_INCOMING_DATAGRAM_STREAM)->obj;
}
FUSEDWASI_P2_FN fusedwasi_p2_dgram_stream *fusedwasi_p2_outgoing_get(fusedwasi_p2_guest *g, uint32_t h) {
    return (fusedwasi_p2_dgram_stream *)fusedwasi_p2_table_get(g, h, FUSEDWASI_P2_KIND_OUTGOING_DATAGRAM_STREAM)->obj;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_incoming_datagram_stream_drop(fusedwasi_p2_guest *g, uint32_t h) {
    fusedwasi_p2_incoming_get(g, h);
    fusedwasi_p2_table_drop(g, h, FUSEDWASI_P2_KIND_INCOMING_DATAGRAM_STREAM);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_outgoing_datagram_stream_drop(fusedwasi_p2_guest *g, uint32_t h) {
    fusedwasi_p2_outgoing_get(g, h);
    fusedwasi_p2_table_drop(g, h, FUSEDWASI_P2_KIND_OUTGOING_DATAGRAM_STREAM);
}

typedef struct {
    uint8_t *data;
    uint32_t len;
    fusedwasi_p2_addr src;
} fusedwasi_p2_received;

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_incoming_datagram_stream_receive(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t max_results, uint32_t retptr) {
    fusedwasi_p2_udp *u = fusedwasi_p2_incoming_get(g, self)->u;
    int rc = fusedwasi_p2_udp_open(u);
    uint32_t count = 0, capacity = 0;
    fusedwasi_p2_received *got = NULL;
    uint8_t *buf = (uint8_t *)malloc(65536); /* scratch only: filled and copied out before the call returns */
    if (!buf) g->trap(g, "out of memory");
    while (rc == FUSEDWASI_P2_NOK && count < max_results) {
        struct sockaddr_storage ss;
        socklen_t n = sizeof ss;
        fusedwasi_p2_ssize r = FUSEDWASI_P2_RECVFROM(u->fd, buf, sizeof buf, 0, (struct sockaddr *)&ss, &n);
        if (r < 0) {
            if (fusedwasi_p2_errno() == EAGAIN || fusedwasi_p2_errno() == EWOULDBLOCK) break;
            if (count) break;
            rc = fusedwasi_p2_net_errno(fusedwasi_p2_errno());
            break;
        }
        fusedwasi_p2_addr src = fusedwasi_p2_from_sockaddr(&ss);
        if (u->has_remote && (src.family != u->remote.family || src.port != u->remote.port || memcmp(src.v4, u->remote.v4, 4) || memcmp(src.v6, u->remote.v6, 16)))
            continue; /* a datagram from another address than the one the stream is for */
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 4;
            got = (fusedwasi_p2_received *)realloc(got, capacity * sizeof *got);
            if (!got) g->trap(g, "out of memory");
        }
        got[count].data = (uint8_t *)malloc(r ? (size_t)r : 1);
        if (!got[count].data) g->trap(g, "out of memory");
        memcpy(got[count].data, buf, (size_t)r);
        got[count].len = (uint32_t)r;
        got[count].src = src;
        count++;
    }
    free(buf);
    if (rc != FUSEDWASI_P2_NOK) {
        for (uint32_t i = 0; i < count; i++) free(got[i].data);
        free(got);
        fusedwasi_p2_net_fail(g, comp, retptr, retptr + 4, rc);
        return;
    }
    uint32_t ptr = fusedwasi_p2_alloc(g, comp, 4, 40 * count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t dptr = fusedwasi_p2_alloc(g, comp, 1, got[i].len);
        if (got[i].len) memcpy(fusedwasi_p2_mem(g, comp, dptr, got[i].len), got[i].data, got[i].len);
        free(got[i].data);
        fusedwasi_p2_store_u32(g, comp, ptr + 40 * i, dptr);
        fusedwasi_p2_store_u32(g, comp, ptr + 40 * i + 4, got[i].len);
        fusedwasi_p2_write_addr(g, comp, ptr + 40 * i + 8, &got[i].src);
    }
    free(got);
    fusedwasi_p2_ok(g, comp, retptr);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, ptr);
    fusedwasi_p2_store_u32(g, comp, retptr + 8, count);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_sockets_udp_incoming_datagram_stream_subscribe(fusedwasi_p2_guest *g, uint32_t self) {
    return fusedwasi_p2_sock_pollable(g, 1, NULL, fusedwasi_p2_incoming_get(g, self)->u);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_outgoing_datagram_stream_check_send(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_dgram_stream *o = fusedwasi_p2_outgoing_get(g, self);
    int rc = fusedwasi_p2_udp_open(o->u);
    if (rc != FUSEDWASI_P2_NOK) {
        fusedwasi_p2_net_fail(g, comp, retptr, retptr + 8, rc);
        return;
    }
    o->permit = FUSEDWASI_P2_CHECK_SEND_PERMIT;
    fusedwasi_p2_ok(g, comp, retptr);
    fusedwasi_p2_store_u64(g, comp, retptr + 8, FUSEDWASI_P2_CHECK_SEND_PERMIT);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_udp_outgoing_datagram_stream_send(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t length, uint32_t retptr) {
    fusedwasi_p2_dgram_stream *o = fusedwasi_p2_outgoing_get(g, self);
    fusedwasi_p2_udp *u = o->u;
    if (length > o->permit) g->trap(g, "send of more datagrams than check-send permitted");
    o->permit = 0;
    uint64_t sent = 0;
    int rc = fusedwasi_p2_udp_open(u);
    for (uint32_t i = 0; rc == FUSEDWASI_P2_NOK && i < length; i++) {
        uint32_t base = ptr + 44 * i;
        uint32_t dptr = fusedwasi_p2_load_u32(g, comp, base), dlen = fusedwasi_p2_load_u32(g, comp, base + 4);
        const uint8_t *data = fusedwasi_p2_mem(g, comp, dptr, dlen);
        int has_remote = fusedwasi_p2_load_u8(g, comp, base + 8) != 0;
        fusedwasi_p2_addr target;
        int e = FUSEDWASI_P2_NOK, would_block = 0;
        if (u->has_remote) {
            if (has_remote) { /* the stream has its peer: the datagram names none, or exactly that one */
                fusedwasi_p2_addr named = fusedwasi_p2_read_addr(g, comp, base + 12);
                if (named.family != u->remote.family || named.port != u->remote.port || memcmp(named.v4, u->remote.v4, 4) || memcmp(named.v6, u->remote.v6, 16) ||
                    named.flow != u->remote.flow || named.scope != u->remote.scope)
                    e = FUSEDWASI_P2_N_INVALID_ARGUMENT;
            }
            target = u->remote;
        } else if (!has_remote) {
            e = FUSEDWASI_P2_N_INVALID_ARGUMENT;
            memset(&target, 0, sizeof target);
        } else {
            target = fusedwasi_p2_read_addr(g, comp, base + 12);
            e = fusedwasi_p2_check_connect(g, u->family, &target, FUSEDWASI_P2_USE_UDP_OUTGOING);
        }
        if (e == FUSEDWASI_P2_NOK) {
            struct sockaddr_storage ss;
            socklen_t n = fusedwasi_p2_to_sockaddr(&target, &ss);
            if (FUSEDWASI_P2_SENDTO(u->fd, data, dlen, FUSEDWASI_P2_MSG_NOSIGNAL, (struct sockaddr *)&ss, n) < 0) {
                if (fusedwasi_p2_errno() == EAGAIN || fusedwasi_p2_errno() == EWOULDBLOCK) would_block = 1;
                else e = fusedwasi_p2_net_errno(fusedwasi_p2_errno());
            }
        }
        if (would_block) break; /* the system cannot take it now: the rest is not sent */
        if (e != FUSEDWASI_P2_NOK) {
            if (!sent) rc = e; /* an error after something was sent is not reported */
            break;
        }
        sent++;
    }
    if (rc != FUSEDWASI_P2_NOK) {
        fusedwasi_p2_net_fail(g, comp, retptr, retptr + 8, rc);
        return;
    }
    fusedwasi_p2_ok(g, comp, retptr);
    fusedwasi_p2_store_u64(g, comp, retptr + 8, sent);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_sockets_udp_outgoing_datagram_stream_subscribe(fusedwasi_p2_guest *g, uint32_t self) {
    return fusedwasi_p2_sock_pollable(g, 2, NULL, fusedwasi_p2_outgoing_get(g, self)->u);
}

/* ip-name-lookup */

FUSEDWASI_P2_FN void fusedwasi_p2_resolved_destroy(fusedwasi_p2_state *state, void *obj) {
    fusedwasi_p2_resolved *r = (fusedwasi_p2_resolved *)obj;
    (void)state;
    free(r->list);
    free(r);
}

FUSEDWASI_P2_FN int fusedwasi_p2_name_chars(const char *name) {
    for (const char *c = name; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '.' || *c == '-' || *c == '_')) return 0;
    return 1;
}

FUSEDWASI_P2_FN int fusedwasi_p2_same_addr(const fusedwasi_p2_addr *a, const fusedwasi_p2_addr *b) {
    return a->family == b->family && !memcmp(a->v4, b->v4, 4) && !memcmp(a->v6, b->v6, 16);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_ip_name_lookup_resolve_addresses(fusedwasi_p2_guest *g, uint32_t comp, uint32_t network, uint32_t ptr, uint32_t length,
                                                              uint32_t retptr) {
    fusedwasi_p2_network_get(g, network);
    const uint8_t *raw = fusedwasi_p2_mem(g, comp, ptr, length);
    int rc = FUSEDWASI_P2_NOK;
    fusedwasi_p2_resolved *r = NULL;
    char *name = (char *)malloc((size_t)length + 1);
    if (!name) g->trap(g, "out of memory");
    memcpy(name, raw, length);
    name[length] = '\0';
    if (!g->state->allow_ip_name_lookup) {
        rc = FUSEDWASI_P2_N_ACCESS_DENIED;
    } else if (length == 0 || memchr(raw, 0, length) || strchr(name, '%')) {
        rc = FUSEDWASI_P2_N_INVALID_ARGUMENT;
    } else {
        for (uint32_t i = 0; i < length; i++)
            if (raw[i] >= 0x80) rc = FUSEDWASI_P2_N_INVALID_ARGUMENT;
    }
    if (rc == FUSEDWASI_P2_NOK) {
        r = (fusedwasi_p2_resolved *)calloc(1, sizeof *r);
        if (!r) g->trap(g, "out of memory");
        struct in_addr a4;
        struct in6_addr a6;
        r->list = (fusedwasi_p2_addr *)calloc(1, sizeof *r->list);
        if (!r->list) g->trap(g, "out of memory");
        if (inet_pton(AF_INET, name, &a4) == 1) {
            r->list[0].family = 0;
            memcpy(r->list[0].v4, &a4, 4);
            r->count = 1;
        } else if (inet_pton(AF_INET6, name, &a6) == 1) {
            r->list[0].family = 1;
            for (int i = 0; i < 8; i++) r->list[0].v6[i] = (uint16_t)(a6.s6_addr[2 * i] << 8 | a6.s6_addr[2 * i + 1]);
            r->count = 1;
        } else if (!fusedwasi_p2_name_chars(name)) {
            rc = FUSEDWASI_P2_N_INVALID_ARGUMENT;
        } else {
            struct addrinfo hints, *res = NULL, *p;
            memset(&hints, 0, sizeof hints);
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            fusedwasi_p2_net_start();
            int gai = getaddrinfo(name, NULL, &hints, &res);
            if (gai != 0) {
                rc = gai == EAI_AGAIN ? FUSEDWASI_P2_N_TEMPORARY_RESOLVER_FAILURE : gai == EAI_FAIL ? FUSEDWASI_P2_N_PERMANENT_RESOLVER_FAILURE : FUSEDWASI_P2_N_NAME_UNRESOLVABLE;
            } else {
                uint32_t cap = 1;
                for (p = res; p; p = p->ai_next) {
                    if (p->ai_family != AF_INET && p->ai_family != AF_INET6) continue;
                    fusedwasi_p2_addr a = fusedwasi_p2_from_sockaddr((struct sockaddr_storage *)p->ai_addr);
                    int dup = 0;
                    for (uint32_t i = 0; i < r->count; i++)
                        if (fusedwasi_p2_same_addr(&r->list[i], &a)) dup = 1;
                    if (dup) continue;
                    if (r->count == cap) {
                        cap *= 2;
                        r->list = (fusedwasi_p2_addr *)realloc(r->list, cap * sizeof *r->list);
                        if (!r->list) g->trap(g, "out of memory");
                    }
                    r->list[r->count++] = a;
                }
                freeaddrinfo(res);
            }
        }
    }
    free(name);
    if (rc != FUSEDWASI_P2_NOK) {
        if (r) fusedwasi_p2_resolved_destroy(g->state, r);
        fusedwasi_p2_net_fail(g, comp, retptr, retptr + 4, rc);
        return;
    }
    fusedwasi_p2_ok(g, comp, retptr);
    fusedwasi_p2_store_u32(g, comp, retptr + 4, fusedwasi_p2_table_new(g, FUSEDWASI_P2_KIND_RESOLVE_STREAM, r, fusedwasi_p2_resolved_destroy));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_ip_name_lookup_resolve_address_stream_resolve_next_address(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_resolved *r = (fusedwasi_p2_resolved *)fusedwasi_p2_table_get(g, self, FUSEDWASI_P2_KIND_RESOLVE_STREAM)->obj;
    fusedwasi_p2_ok(g, comp, retptr);
    if (r->pos >= r->count) {
        fusedwasi_p2_store_u8(g, comp, retptr + 2, 0);
        return;
    }
    const fusedwasi_p2_addr *a = &r->list[r->pos++];
    fusedwasi_p2_store_u8(g, comp, retptr + 2, 1);
    fusedwasi_p2_store_u8(g, comp, retptr + 4, (uint8_t)a->family);
    if (a->family == 0) memcpy(fusedwasi_p2_mem(g, comp, retptr + 6, 4), a->v4, 4);
    else
        for (int i = 0; i < 8; i++) fusedwasi_p2_store_u16(g, comp, retptr + 6 + 2 * i, a->v6[i]);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_sockets_ip_name_lookup_resolve_address_stream_subscribe(fusedwasi_p2_guest *g, uint32_t self) {
    fusedwasi_p2_table_get(g, self, FUSEDWASI_P2_KIND_RESOLVE_STREAM);
    return fusedwasi_p2_sock_pollable(g, 4, NULL, NULL);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_sockets_ip_name_lookup_resolve_address_stream_drop(fusedwasi_p2_guest *g, uint32_t h) {
    fusedwasi_p2_table_get(g, h, FUSEDWASI_P2_KIND_RESOLVE_STREAM);
    fusedwasi_p2_table_drop(g, h, FUSEDWASI_P2_KIND_RESOLVE_STREAM);
}

#endif /* FUSEDWASI_P2_SOCKETS_H */
