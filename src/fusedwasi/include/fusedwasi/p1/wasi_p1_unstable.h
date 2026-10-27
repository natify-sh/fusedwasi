/* WASI 0.1, `wasi_unstable` (the first snapshot, "preview0"): the calls are those of wasi_p1_core.h, named `fusedwasi_p1_<name>`, except
   those that read or write what differs from `wasi_snapshot_preview1` (see wasi_p1_core.h), which are `fusedwasi_p1_unstable_<name>` and
   are defined here. `sock_accept` is not a call of this snapshot. */
#ifndef FUSEDWASI_P1_UNSTABLE_H
#define FUSEDWASI_P1_UNSTABLE_H

#include "wasi_p1_core.h"

/* the order of whence in this snapshot: cur, end, set */
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_unstable_fd_seek(fusedwasi_p2_guest *g, uint32_t fd, uint64_t offset, uint32_t whence, uint32_t ptr) {
    static const int native[3] = {SEEK_CUR, SEEK_END, SEEK_SET};
    if (whence > 2) return FUSEDWASI_P1_E_INVAL;
    return fusedwasi_p1_seek_native(g, fd, (int64_t)offset, native[whence], ptr, FUSEDWASI_P1_SEEK_RIGHT((int64_t)offset, native[whence]));
}

/* this snapshot has one right less, the last (sock_accept): the rights that a descriptor tells are of 29 bits */
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_unstable_fd_fdstat_get(fusedwasi_p2_guest *g, uint32_t fd, uint32_t ptr) {
    uint32_t rc = fusedwasi_p1_fd_fdstat_get(g, fd, ptr);
    if (rc == FUSEDWASI_P1_E_SUCCESS) {
        uint64_t base = 0, inheriting = 0;
        const uint64_t mask = ((uint64_t)1 << 29) - 1;
        memcpy(&base, fusedwasi_p1_mem(g) + ptr + 8, 8);  /* (little-endian hosts) */
        memcpy(&inheriting, fusedwasi_p1_mem(g) + ptr + 16, 8);
        fusedwasi_p1_put_u64(g, ptr + 8, base & mask);
        fusedwasi_p1_put_u64(g, ptr + 16, inheriting & mask);
    }
    return rc;
}

/* the filestat of this snapshot has a 32-bit link count (56 bytes) */
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_unstable_fd_filestat_get(fusedwasi_p2_guest *g, uint32_t fd, uint32_t ptr) { return fusedwasi_p1_filestat_of_fd(g, fd, ptr, 1); }

FUSEDWASI_P1_FN uint32_t fusedwasi_p1_unstable_path_filestat_get(fusedwasi_p2_guest *g, uint32_t fd, uint32_t flags, uint32_t path, uint32_t len, uint32_t ptr) {
    return fusedwasi_p1_filestat_of_path(g, fd, flags, path, len, ptr, 1);
}

/* the clock subscription of this snapshot starts with an identifier (the subscriptions are 56 bytes) */
FUSEDWASI_P1_FN uint32_t fusedwasi_p1_unstable_poll_oneoff(fusedwasi_p2_guest *g, uint32_t in, uint32_t out, uint32_t count, uint32_t nevents) {
    return fusedwasi_p1_poll(g, in, out, count, nevents, 1);
}

#endif
