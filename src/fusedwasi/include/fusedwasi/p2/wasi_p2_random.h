/* wasi:random (5 imports): random, insecure and insecure-seed (the twin of p2/wasi_p2_random.py).
 *
 * The bytes come from `state->random_bytes` and, for the insecure interfaces, `state->insecure_random_bytes` (the same source unless replaced); a source that fails is a trap.
 * A list is returned through the pointer that is the last argument, in memory of the component (`cabi_realloc`); insecure-seed is a tuple of two u64 (16 bytes).
 * A request of more than FUSEDWASI_P2__RANDOM_MAX bytes is a trap, so that a guest cannot ask the host for all its memory.
 *
 * Host API (docs/public_api.md): the functions of its imports (`c_name` in signatures/wasi_p2.json); no other name is public.
 * Every other name in this file is private: a double underscore after the prefix (`fusedwasi_p2__x`, `FUSEDWASI_P2__X`), and a private field ends with two underscores (`x__`). */
#ifndef FUSEDWASI_P2__RANDOM_H
#define FUSEDWASI_P2__RANDOM_H

#include "wasi_p2_core.h"

#define FUSEDWASI_P2__RANDOM_MAX (1u << 26)

FUSEDWASI_P2_FN void fusedwasi_p2__random_fill(fusedwasi_p2_guest *g, int insecure, uint8_t *buf, size_t n) {
    int (*source)(fusedwasi_p2_state *, uint8_t *, size_t) = insecure ? g->state->insecure_random_bytes : g->state->random_bytes;
    if (n && source(g->state, buf, n) != 0) g->trap(g, "the source of random bytes failed");
}

FUSEDWASI_P2_FN void fusedwasi_p2__random_bytes(fusedwasi_p2_guest *g, uint32_t comp, int insecure, uint64_t len, uint32_t retptr) {
    if (len > FUSEDWASI_P2__RANDOM_MAX) g->trap(g, "too many random bytes requested");
    uint32_t n = (uint32_t)len;
    uint32_t ptr = fusedwasi_p2__alloc(g, comp, 1, n);
    fusedwasi_p2__random_fill(g, insecure, fusedwasi_p2__mem(g, comp, ptr, n), n);
    fusedwasi_p2__store_u32(g, comp, retptr, ptr);
    fusedwasi_p2__store_u32(g, comp, retptr + 4, n);
}

FUSEDWASI_P2_FN uint64_t fusedwasi_p2__random_u64(fusedwasi_p2_guest *g, int insecure) {
    uint8_t b[8];
    uint64_t v = 0;
    fusedwasi_p2__random_fill(g, insecure, b, 8);
    for (int i = 0; i < 8; i++) v |= (uint64_t)b[i] << (8 * i);
    return v;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_random_random_get_random_bytes(fusedwasi_p2_guest *g, uint32_t comp, uint64_t len, uint32_t retptr) {
    fusedwasi_p2__random_bytes(g, comp, 0, len, retptr);
}
FUSEDWASI_P2_FN uint64_t fusedwasi_wasi_random_random_get_random_u64(fusedwasi_p2_guest *g) { return fusedwasi_p2__random_u64(g, 0); }

FUSEDWASI_P2_FN void fusedwasi_wasi_random_insecure_get_insecure_random_bytes(fusedwasi_p2_guest *g, uint32_t comp, uint64_t len, uint32_t retptr) {
    fusedwasi_p2__random_bytes(g, comp, 1, len, retptr);
}
FUSEDWASI_P2_FN uint64_t fusedwasi_wasi_random_insecure_get_insecure_random_u64(fusedwasi_p2_guest *g) { return fusedwasi_p2__random_u64(g, 1); }

FUSEDWASI_P2_FN void fusedwasi_wasi_random_insecure_seed_insecure_seed(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr) {
    uint64_t a = fusedwasi_p2__random_u64(g, 1), b = fusedwasi_p2__random_u64(g, 1); /* in this order */
    fusedwasi_p2__store_u64(g, comp, retptr, a);
    fusedwasi_p2__store_u64(g, comp, retptr + 8, b);
}

#endif /* FUSEDWASI_P2__RANDOM_H */
