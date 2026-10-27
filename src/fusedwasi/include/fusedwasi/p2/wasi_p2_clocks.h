/* wasi:clocks (6 imports): `wall-clock` and `monotonic-clock` (the twin of p2/wasi_p2_clocks.py).
 *
 * Names carry the interface: fusedwasi_wasi_clocks_<interface>_<function>. A result that does not fit in one flat value is written through the pointer that is the last
 * argument (the canonical ABI); `datetime` is a record {seconds: u64, nanoseconds: u32} of 16 bytes, 8-aligned. */
#ifndef FUSEDWASI_P2_CLOCKS_H
#define FUSEDWASI_P2_CLOCKS_H

#include "wasi_p2_core.h"

FUSEDWASI_P2_FN void fusedwasi_p2_datetime(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, uint64_t ns) {
    fusedwasi_p2_store_u64(g, comp, retptr, ns / 1000000000ULL);
    fusedwasi_p2_store_u32(g, comp, retptr + 8, (uint32_t)(ns % 1000000000ULL));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_clocks_wall_clock_now(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr) {
    fusedwasi_p2_datetime(g, comp, retptr, fusedwasi_p2_clock_ns(g, 0));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_clocks_wall_clock_resolution(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr) {
    fusedwasi_p2_datetime(g, comp, retptr, g->state->resolution_ns[0]);
}

FUSEDWASI_P2_FN uint64_t fusedwasi_wasi_clocks_monotonic_clock_now(fusedwasi_p2_guest *g) { return fusedwasi_p2_clock_ns(g, 1); }

FUSEDWASI_P2_FN uint64_t fusedwasi_wasi_clocks_monotonic_clock_resolution(fusedwasi_p2_guest *g) { return g->state->resolution_ns[1]; }

/* a pollable that is ready at an instant of the monotonic clock; a clock that cannot be read is -1 ("the clock cannot be read": the WASI function that asked traps with it) */
FUSEDWASI_P2_FN int fusedwasi_p2_deadline_ready(fusedwasi_p2_state *state, fusedwasi_p2_pollable *p) {
    uint64_t now = 0;
    if (state->clock(state, 1, &now) != 0) {
        p->failure = "the clock cannot be read";
        return -1;
    }
    return now >= p->deadline;
}

FUSEDWASI_P2_FN void fusedwasi_p2_free_obj(fusedwasi_p2_state *state, void *obj) {
    (void)state;
    free(obj);
}

/* A deadline pollable in the table of the state: the handle, or 0 when there is no memory or the table is full. */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2_deadline_pollable_add(fusedwasi_p2_state *state, uint64_t deadline) {
    fusedwasi_p2_pollable *p = (fusedwasi_p2_pollable *)calloc(1, sizeof *p);
    if (!p) return 0;
    p->ready = fusedwasi_p2_deadline_ready;
    p->deadline = deadline;
    uint32_t handle = fusedwasi_p2_table_add(state, FUSEDWASI_P2_KIND_POLLABLE, p, fusedwasi_p2_free_obj);
    if (!handle) free(p);
    return handle;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_p2_deadline_pollable(fusedwasi_p2_guest *g, uint64_t deadline) {
    uint32_t handle = fusedwasi_p2_deadline_pollable_add(g->state, deadline);
    if (!handle) g->trap(g, g->state->slot_count > FUSEDWASI_P2_INDEX_MASK ? "table of handles is full" : "out of memory");
    return handle;
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_clocks_monotonic_clock_subscribe_instant(fusedwasi_p2_guest *g, uint64_t when) {
    return fusedwasi_p2_deadline_pollable(g, when);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_clocks_monotonic_clock_subscribe_duration(fusedwasi_p2_guest *g, uint64_t when) {
    uint64_t now = fusedwasi_p2_clock_ns(g, 1);
    return fusedwasi_p2_deadline_pollable(g, when > UINT64_MAX - now ? UINT64_MAX : now + when);
}

#endif /* FUSEDWASI_P2_CLOCKS_H */
