/* The checks that the host makes on an address before a bind or a connect (the rules of wasi:sockets: a unicast address of the family of the socket; a connect also needs an address that is
   not the unspecified one and a port that is not 0; then the policy of the embedder), for IPv4 and for IPv6. They are pure: no socket is made, so this runs on a machine with no IPv6.
   The output is compared with the Python twin's (tests/test_wasi_p2_address_checks.py): a line for each address, `<label>: bind=<result> connect=<result>`. */
#include <stdio.h>
#include <string.h>

#include "wasi_p2_sockets.h"

/* the policy of the embedder: everything is allowed except port 9 */
static int policy(fusedwasi_p2_state *s, const char *ip, uint16_t port, int use) {
    (void)s, (void)ip, (void)use;
    return port != 9;
}

struct item {
    const char *label;
    int socket_family; /* 0 IPv4, 1 IPv6 */
    int family;        /* of the address */
    uint16_t port;
    uint8_t v4[4];
    uint16_t v6[8];
};

static const struct item ITEMS[] = {
    {"v6 loopback", 1, 1, 80, {0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 1}},
    {"v6 unspecified", 1, 1, 80, {0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}},
    {"v6 unspecified, port 0", 1, 1, 0, {0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}},
    {"v6 loopback, port 0", 1, 1, 0, {0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 1}},
    {"v6 multicast", 1, 1, 80, {0, 0, 0, 0}, {0xff02, 0, 0, 0, 0, 0, 0, 1}},
    {"v6 ipv4-mapped", 1, 1, 80, {0, 0, 0, 0}, {0, 0, 0, 0, 0, 0xffff, 0x7f00, 0x0001}},
    {"v6 documentation", 1, 1, 80, {0, 0, 0, 0}, {0x2001, 0x0db8, 0, 0, 0, 0, 0, 1}},
    {"v4 on a v6 socket", 1, 0, 80, {127, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 0, 0}},
    {"v6 on a v4 socket", 0, 1, 80, {0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 1}},
    {"v4 loopback", 0, 0, 80, {127, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 0, 0}},
    {"v4 unspecified", 0, 0, 80, {0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}},
    {"v4 broadcast", 0, 0, 80, {255, 255, 255, 255}, {0, 0, 0, 0, 0, 0, 0, 0}},
    {"v4 multicast", 0, 0, 80, {224, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 0, 0}},
    {"v4 loopback, port 0", 0, 0, 0, {127, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 0, 0}},
    {"v6 loopback, refused by the policy", 1, 1, 9, {0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 1}},
    {"v4 loopback, refused by the policy", 0, 0, 9, {127, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 0, 0}},
};

static const char *name(int rc) {
    if (rc == FUSEDWASI_P2__NOK) return "ok";
    if (rc == FUSEDWASI_P2_N_INVALID_ARGUMENT) return "invalid-argument";
    if (rc == FUSEDWASI_P2_N_ACCESS_DENIED) return "access-denied";
    return "other";
}

int main(void) {
    fusedwasi_p2_state state;
    fusedwasi_p2_guest guest;
    fusedwasi_p2_init(&state);
    state.socket_addr_check = policy;
    fusedwasi_p2_guest_init(&guest, &state);
    for (size_t i = 0; i < sizeof ITEMS / sizeof ITEMS[0]; i++) {
        const struct item *it = &ITEMS[i];
        fusedwasi_p2__addr a;
        memset(&a, 0, sizeof a);
        a.family = it->family;
        a.port = it->port;
        memcpy(a.v4, it->v4, sizeof a.v4);
        memcpy(a.v6, it->v6, sizeof a.v6);
        int bind_rc = fusedwasi_p2__check_bind(&guest, it->socket_family, &a, FUSEDWASI_P2_USE_TCP_BIND);
        int connect_rc = fusedwasi_p2__check_connect(&guest, it->socket_family, &a, FUSEDWASI_P2_USE_TCP_CONNECT);
        printf("%s: bind=%s connect=%s\n", it->label, name(bind_rc), name(connect_rc));
    }
    fusedwasi_p2_free(&state);
    return 0;
}
