/* wasi:clocks (6 imports): `wall-clock` and `monotonic-clock` (the twin of p2/wasi_p2_clocks.py).
 *
 * Names carry the interface: fusedwasi_wasi_clocks_<interface>_<function>. A result that does not fit in one flat value is written through the pointer that is the last
 * argument (the canonical ABI); `datetime` is a record {seconds: u64, nanoseconds: u32} of 16 bytes, 8-aligned. */
#ifndef FUSEDWASI_P2_CLOCKS_H
#define FUSEDWASI_P2_CLOCKS_H

#include "wasi_p2_core.h"

FUSEDWASI_P2_FN void fusedwasi_p2_datetime(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr, uint64_t ns) {
    fusedwasi_p2_store_u64(s, comp, retptr, ns / 1000000000ULL);
    fusedwasi_p2_store_u32(s, comp, retptr + 8, (uint32_t)(ns % 1000000000ULL));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_clocks_wall_clock_now(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr) {
    fusedwasi_p2_datetime(s, comp, retptr, s->wall_ns(s));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_clocks_wall_clock_resolution(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr) {
    fusedwasi_p2_datetime(s, comp, retptr, s->wall_resolution_ns);
}

FUSEDWASI_P2_FN uint64_t fusedwasi_wasi_clocks_monotonic_clock_now(fusedwasi_p2_state *s) { return s->monotonic_ns(s); }

FUSEDWASI_P2_FN uint64_t fusedwasi_wasi_clocks_monotonic_clock_resolution(fusedwasi_p2_state *s) { return s->monotonic_resolution_ns; }

/* a pollable that is ready at an instant of the monotonic clock */
FUSEDWASI_P2_FN int fusedwasi_p2_deadline_ready(fusedwasi_p2_state *s, fusedwasi_p2_pollable *p) { return s->monotonic_ns(s) >= p->deadline; }

FUSEDWASI_P2_FN void fusedwasi_p2_free_obj(fusedwasi_p2_state *s, void *obj) {
    (void)s;
    free(obj);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_deadline_pollable(fusedwasi_p2_state *s, uint64_t deadline) {
    fusedwasi_p2_pollable *p = (fusedwasi_p2_pollable *)calloc(1, sizeof *p);
    if (!p) s->trap(s, "out of memory");
    p->ready = fusedwasi_p2_deadline_ready;
    p->deadline = deadline;
    return fusedwasi_p2_table_new(s, FUSEDWASI_P2_KIND_POLLABLE, p, fusedwasi_p2_free_obj);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_clocks_monotonic_clock_subscribe_instant(fusedwasi_p2_state *s, uint64_t when) {
    return fusedwasi_p2_deadline_pollable(s, when);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_clocks_monotonic_clock_subscribe_duration(fusedwasi_p2_state *s, uint64_t when) {
    uint64_t now = s->monotonic_ns(s);
    return fusedwasi_p2_deadline_pollable(s, when > UINT64_MAX - now ? UINT64_MAX : now + when);
}

#endif /* FUSEDWASI_P2_CLOCKS_H */
