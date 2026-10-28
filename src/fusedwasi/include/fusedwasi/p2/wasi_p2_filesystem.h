/* wasi:filesystem (32 imports): preopens and types (the twin of p2/wasi_p2_filesystem.py, where the confinement of paths, the rights and the layouts are described).
 * POSIX (openat and the rest of the *at calls: wasi_p2_fs_posix.h) and Windows (paths below a directory: wasi_p2_fs_win32.h). The state keeps the preopens:
 * `state->preopens`, a list of {path, name, writable} owned by the caller. Every function that can fail with an error-code writes it in the result; a bad handle or a pointer
 * outside the memory is a trap.
 *
 * Host API (docs/public_api.md): the functions of its imports (`c_name` in signatures/wasi_p2.json), and
 *   FUSEDWASI_P2_E_*.
 * Every other name in this file is private: a double underscore after the prefix (`fusedwasi_p2__x`, `FUSEDWASI_P2__X`), and a private field ends with two underscores (`x__`). */
#ifndef FUSEDWASI_P2__FILESYSTEM_H
#define FUSEDWASI_P2__FILESYSTEM_H

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "wasi_p2_io.h"

/* error-code, in the order of the WIT */
enum {
    FUSEDWASI_P2_E_ACCESS, FUSEDWASI_P2_E_WOULD_BLOCK, FUSEDWASI_P2_E_ALREADY, FUSEDWASI_P2_E_BAD_DESCRIPTOR, FUSEDWASI_P2_E_BUSY, FUSEDWASI_P2_E_DEADLOCK, FUSEDWASI_P2_E_QUOTA,
    FUSEDWASI_P2_E_EXIST, FUSEDWASI_P2_E_FILE_TOO_LARGE, FUSEDWASI_P2_E_ILLEGAL_BYTE_SEQUENCE, FUSEDWASI_P2_E_IN_PROGRESS, FUSEDWASI_P2_E_INTERRUPTED, FUSEDWASI_P2_E_INVALID,
    FUSEDWASI_P2_E_IO, FUSEDWASI_P2_E_IS_DIRECTORY, FUSEDWASI_P2_E_LOOP, FUSEDWASI_P2_E_TOO_MANY_LINKS, FUSEDWASI_P2_E_MESSAGE_SIZE, FUSEDWASI_P2_E_NAME_TOO_LONG,
    FUSEDWASI_P2_E_NO_DEVICE, FUSEDWASI_P2_E_NO_ENTRY, FUSEDWASI_P2_E_NO_LOCK, FUSEDWASI_P2_E_INSUFFICIENT_MEMORY, FUSEDWASI_P2_E_INSUFFICIENT_SPACE, FUSEDWASI_P2_E_NOT_DIRECTORY,
    FUSEDWASI_P2_E_NOT_EMPTY, FUSEDWASI_P2_E_NOT_RECOVERABLE, FUSEDWASI_P2_E_UNSUPPORTED, FUSEDWASI_P2_E_NO_TTY, FUSEDWASI_P2_E_NO_SUCH_DEVICE, FUSEDWASI_P2_E_OVERFLOW,
    FUSEDWASI_P2_E_NOT_PERMITTED, FUSEDWASI_P2_E_PIPE, FUSEDWASI_P2_E_READ_ONLY, FUSEDWASI_P2_E_INVALID_SEEK, FUSEDWASI_P2_E_TEXT_FILE_BUSY, FUSEDWASI_P2_E_CROSS_DEVICE
};
#define FUSEDWASI_P2__OK (-1) /* what the functions of this file return when nothing failed; an error-code otherwise */

/* descriptor-flags, path-flags, open-flags */
enum { FUSEDWASI_P2__READ = 1, FUSEDWASI_P2__WRITE = 2, FUSEDWASI_P2__FILE_SYNC = 4, FUSEDWASI_P2__DATA_SYNC = 8, FUSEDWASI_P2__REQUESTED_SYNC = 16, FUSEDWASI_P2__MUTATE = 32 };
enum { FUSEDWASI_P2__SYMLINK_FOLLOW = 1 };
enum { FUSEDWASI_P2__CREATE = 1, FUSEDWASI_P2__DIRECTORY = 2, FUSEDWASI_P2__EXCLUSIVE = 4, FUSEDWASI_P2__TRUNCATE = 8 };
/* descriptor-type */
enum { FUSEDWASI_P2__T_UNKNOWN, FUSEDWASI_P2__T_BLOCK, FUSEDWASI_P2__T_CHAR, FUSEDWASI_P2__T_DIRECTORY, FUSEDWASI_P2__T_FIFO, FUSEDWASI_P2__T_SYMLINK, FUSEDWASI_P2__T_REGULAR, FUSEDWASI_P2__T_SOCKET };

#define FUSEDWASI_P2__NAME_MAX 255
#define FUSEDWASI_P2__MAX_DEPTH 128
#define FUSEDWASI_P2__MAX_LINKS 40
#define FUSEDWASI_P2__PATH_MAX 8192
#define FUSEDWASI_P2__STREAM_CHUNK 65536u

/* what the system says of a file (for both systems): times in nanoseconds since 1970 */
typedef struct {
    uint8_t type;
    uint64_t nlink, size, dev, ino;
    int64_t atime_ns, mtime_ns, ctime_ns;
} fusedwasi_p2__stat;

typedef struct {
    int fd; /* POSIX: the descriptor; Windows: the C-library descriptor of a file, -1 for a directory */
    uint32_t flags;
    int is_dir;
    char *path; /* Windows: where it is, as UTF-8 with the \\?\ prefix (malloc'd); POSIX: NULL */
} fusedwasi_p2__descriptor;

typedef struct {
    uint32_t count, pos;
    uint8_t *types;
    char **names;
} fusedwasi_p2__dir_entries;

FUSEDWASI_P2_FN int fusedwasi_p2__errno_code(int e) {
    switch (e) {
    case EACCES: return FUSEDWASI_P2_E_ACCESS;
    case EAGAIN: return FUSEDWASI_P2_E_WOULD_BLOCK;
#ifdef EALREADY
    case EALREADY: return FUSEDWASI_P2_E_ALREADY;
#endif
    case EBADF: return FUSEDWASI_P2_E_BAD_DESCRIPTOR;
    case EBUSY: return FUSEDWASI_P2_E_BUSY;
    case EDEADLK: return FUSEDWASI_P2_E_DEADLOCK;
#ifdef EDQUOT
    case EDQUOT: return FUSEDWASI_P2_E_QUOTA;
#endif
    case EEXIST: return FUSEDWASI_P2_E_EXIST;
    case EFBIG: return FUSEDWASI_P2_E_FILE_TOO_LARGE;
    case EILSEQ: return FUSEDWASI_P2_E_ILLEGAL_BYTE_SEQUENCE;
#ifdef EINPROGRESS
    case EINPROGRESS: return FUSEDWASI_P2_E_IN_PROGRESS;
#endif
    case EINTR: return FUSEDWASI_P2_E_INTERRUPTED;
    case EINVAL: return FUSEDWASI_P2_E_INVALID;
    case EIO: return FUSEDWASI_P2_E_IO;
    case EISDIR: return FUSEDWASI_P2_E_IS_DIRECTORY;
#ifdef ELOOP
    case ELOOP: return FUSEDWASI_P2_E_LOOP;
#endif
    case EMLINK: return FUSEDWASI_P2_E_TOO_MANY_LINKS;
#ifdef EMSGSIZE
    case EMSGSIZE: return FUSEDWASI_P2_E_MESSAGE_SIZE;
#endif
    case ENAMETOOLONG: return FUSEDWASI_P2_E_NAME_TOO_LONG;
    case ENODEV: return FUSEDWASI_P2_E_NO_DEVICE;
    case ENOENT: return FUSEDWASI_P2_E_NO_ENTRY;
    case ENOLCK: return FUSEDWASI_P2_E_NO_LOCK;
    case ENOMEM: return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
    case ENOSPC: return FUSEDWASI_P2_E_INSUFFICIENT_SPACE;
    case ENOTDIR: return FUSEDWASI_P2_E_NOT_DIRECTORY;
    case ENOTEMPTY: return FUSEDWASI_P2_E_NOT_EMPTY;
#ifdef ENOTRECOVERABLE
    case ENOTRECOVERABLE: return FUSEDWASI_P2_E_NOT_RECOVERABLE;
#endif
    case ENOSYS: return FUSEDWASI_P2_E_UNSUPPORTED;
#ifdef ENOTSUP
    case ENOTSUP: return FUSEDWASI_P2_E_UNSUPPORTED; /* where EOPNOTSUPP is the same number (Linux) it is this one; the Python twin maps ENOTSUP only */
#endif
    case ENOTTY: return FUSEDWASI_P2_E_NO_TTY;
    case ENXIO: return FUSEDWASI_P2_E_NO_SUCH_DEVICE;
#ifdef EOVERFLOW
    case EOVERFLOW: return FUSEDWASI_P2_E_OVERFLOW;
#endif
    case EPERM: return FUSEDWASI_P2_E_NOT_PERMITTED;
    case EPIPE: return FUSEDWASI_P2_E_PIPE;
    case EROFS: return FUSEDWASI_P2_E_READ_ONLY;
    case ESPIPE: return FUSEDWASI_P2_E_INVALID_SEEK;
#ifdef ETXTBSY
    case ETXTBSY: return FUSEDWASI_P2_E_TEXT_FILE_BUSY;
#endif
    case EXDEV: return FUSEDWASI_P2_E_CROSS_DEVICE;
    default: return FUSEDWASI_P2_E_IO;
    }
}

/* the code for the last failed call */
FUSEDWASI_P2_FN int fusedwasi_p2__fail(void) { return fusedwasi_p2__errno_code(errno); }

#ifdef _WIN32
#include "wasi_p2_fs_win32.h"
#else
#include "wasi_p2_fs_posix.h"
#endif

/* the descriptors and what they stand for */

FUSEDWASI_P2_FN void fusedwasi_p2__descriptor_destroy(fusedwasi_p2_state *state, void *obj) {
    fusedwasi_p2__descriptor *d = (fusedwasi_p2__descriptor *)obj;
    (void)state;
    fusedwasi_p2__sys_close(d);
    free(d);
}

FUSEDWASI_P2_FN void fusedwasi_p2__dir_entries_destroy(fusedwasi_p2_state *state, void *obj) {
    fusedwasi_p2__dir_entries *e = (fusedwasi_p2__dir_entries *)obj;
    (void)state;
    for (uint32_t i = 0; i < e->count; i++) free(e->names[i]);
    free(e->names);
    free(e->types);
    free(e);
}

FUSEDWASI_P2_FN fusedwasi_p2__descriptor *fusedwasi_p2__descriptor_get(fusedwasi_p2_guest *g, uint32_t handle) {
    return (fusedwasi_p2__descriptor *)fusedwasi_p2__table_get(g, handle, FUSEDWASI_P2_KIND_DESCRIPTOR)->obj;
}

/* the rights: FUSEDWASI_P2__OK, or the error-code that the absence of the flag is */
FUSEDWASI_P2_FN int fusedwasi_p2__need(const fusedwasi_p2__descriptor *d, uint32_t flag) {
    if (d->flags & flag) return FUSEDWASI_P2__OK;
    return flag == FUSEDWASI_P2__MUTATE ? FUSEDWASI_P2_E_READ_ONLY : FUSEDWASI_P2_E_BAD_DESCRIPTOR; /* `mutate-directory` unset: read-only (the WIT says so) */
}

/* a handle on a descriptor that the system call filled (`made` is copied); 0 when there is no memory or the table is full (the function that has the guest traps: `fusedwasi_p2__handle_or_trap`) */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2__descriptor_new(fusedwasi_p2_state *state, const fusedwasi_p2__descriptor *made, uint32_t flags) {
    fusedwasi_p2__descriptor *d = (fusedwasi_p2__descriptor *)malloc(sizeof *d);
    if (!d) return 0;
    *d = *made;
    d->flags = flags;
    uint32_t handle = fusedwasi_p2_table_add(state, FUSEDWASI_P2_KIND_DESCRIPTOR, d, fusedwasi_p2__descriptor_destroy);
    if (!handle) free(d);
    return handle;
}

/* the descriptor of a live handle, NULL for any other (no trap: the adapter of WASI 0.1 holds handles of its own) */
FUSEDWASI_P2_FN fusedwasi_p2__descriptor *fusedwasi_p2__descriptor_find(fusedwasi_p2_state *state, uint32_t handle) {
    fusedwasi_p2__slot *slot = fusedwasi_p2__table_find(state, handle, FUSEDWASI_P2_KIND_DESCRIPTOR);
    return slot ? (fusedwasi_p2__descriptor *)slot->obj : NULL;
}

/* ---- the operations ---------------------------------------------------------------------------------------------------------------------------------------------
 * A function of wasi:filesystem is an operation and the Canonical ABI around it. The operation takes host values (a descriptor, a path as bytes and its length, an offset, a buffer) and
 * returns FUSEDWASI_P2__OK or an error-code; it never reads the memory of a guest and never traps. The function of 0.2 below reads its arguments from the memory of the guest and writes the
 * result there; the adapter of WASI 0.1 (p1/wasi_p1_core.h) calls the same operations with its own arguments, so a rule of 0.2 is written once.
 *
 * `opts` is 0 for a function of 0.2 and FUSEDWASI_P2__FS_P1 for the adapter of 0.1, which keeps rights of its own and has checked them: the flags of the descriptor (read, write, mutate-directory)
 * are not asked, the walk of a path is the one of 0.1 (a link with a slash after it, where links are not followed, is not a directory), a way out of the directory is FUSEDWASI_P2__E_ESCAPE (0.2: not-permitted),
 * and the name `.` as the last name of a path answers as 0.1 does (unlink and remove: invalid; rename and link: invalid or exist; make and symlink: exist; readlink: invalid). */
enum { FUSEDWASI_P2__FS_P1 = 1 };
#define FUSEDWASI_P2__E_ESCAPE 100   /* beyond the codes of the WIT; only with FUSEDWASI_P2__FS_P1 */
#define FUSEDWASI_P2__E_NO_CLOCK 101 /* beyond the codes of the WIT: `state->clock` failed; the WASI function that asked has the guest and traps */

FUSEDWASI_P2_FN int fusedwasi_p2__fs_need(const fusedwasi_p2__descriptor *d, uint32_t flag, uint32_t opts) { return (opts & FUSEDWASI_P2__FS_P1) ? FUSEDWASI_P2__OK : fusedwasi_p2__need(d, flag); }

/* the walk of a path below the directory `d` */
FUSEDWASI_P2_FN int fusedwasi_p2__fs_walk(fusedwasi_p2__walk *w, const fusedwasi_p2__descriptor *d, const uint8_t *path, uint32_t len, int follow, uint32_t opts) {
    if (!d->is_dir) return FUSEDWASI_P2_E_NOT_DIRECTORY;
    int rc = fusedwasi_p2__walk_path_with(w, d, path, len, follow, (opts & FUSEDWASI_P2__FS_P1) != 0);
    return (opts & FUSEDWASI_P2__FS_P1) && rc == FUSEDWASI_P2_E_NOT_PERMITTED ? FUSEDWASI_P2__E_ESCAPE : rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2__fs_is_itself(const fusedwasi_p2__walk *w, uint32_t opts) { return (opts & FUSEDWASI_P2__FS_P1) && strcmp(w->leaf, ".") == 0; }

FUSEDWASI_P2_FN int fusedwasi_p2__fs_stat(const fusedwasi_p2__descriptor *d, fusedwasi_p2__stat *out) { return fusedwasi_p2__sys_fstat(d, out); }

/* the stat of a name directly in the directory (`.` and `..` too), no link followed, no walk: what `fd_readdir` of 0.1 needs for the inode */
FUSEDWASI_P2_FN int fusedwasi_p2__fs_stat_entry(const fusedwasi_p2__descriptor *d, const char *name, fusedwasi_p2__stat *out) { return fusedwasi_p2__sys_lstat(FUSEDWASI_P2__DIRREF_OF(d), name, out); }

/* read at an offset: the bytes are in `buf`, `*got` of them (0 at the end of the file) */
FUSEDWASI_P2_FN int fusedwasi_p2__fs_read(const fusedwasi_p2__descriptor *d, uint8_t *buf, uint64_t len, uint64_t offset, uint64_t *got, uint32_t opts) {
    *got = 0;
    int rc = fusedwasi_p2__fs_need(d, FUSEDWASI_P2__READ, opts);
    if (rc != FUSEDWASI_P2__OK) return rc;
    int64_t n = fusedwasi_p2__sys_pread(d->fd, buf, (size_t)len, offset, &rc);
    if (n < 0) return rc;
    *got = (uint64_t)n;
    return FUSEDWASI_P2__OK;
}

/* write at an offset, all of it or an error (`*done` is what was written before the error) */
FUSEDWASI_P2_FN int fusedwasi_p2__fs_write(const fusedwasi_p2__descriptor *d, const uint8_t *data, uint64_t len, uint64_t offset, uint64_t *done, uint32_t opts) {
    *done = 0;
    int rc = fusedwasi_p2__fs_need(d, FUSEDWASI_P2__WRITE, opts);
    while (rc == FUSEDWASI_P2__OK && *done < len) {
        int64_t n = fusedwasi_p2__sys_pwrite(d->fd, data + *done, (size_t)(len - *done), offset + *done, &rc);
        if (n >= 0) *done += (uint64_t)n;
    }
    return rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2__fs_set_size(const fusedwasi_p2__descriptor *d, uint64_t size, uint32_t opts) {
    int rc = fusedwasi_p2__fs_need(d, FUSEDWASI_P2__WRITE, opts);
    return rc == FUSEDWASI_P2__OK ? fusedwasi_p2__sys_ftruncate(d->fd, size) : rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2__fs_sync(const fusedwasi_p2__descriptor *d, int data_only) { return fusedwasi_p2__sys_fsync(d, data_only); }

/* new-timestamp: 0 no-change, 1 now, 2 timestamp (`ns` since 1970) */
FUSEDWASI_P2_FN int64_t fusedwasi_p2__new_time(uint32_t tag, int64_t ns, int64_t current_ns, int64_t now_ns) {
    if (tag == 0) return current_ns;
    if (tag == 1) return now_ns;
    return ns;
}

typedef struct {
    uint32_t a_tag, m_tag;
    int64_t a_ns, m_ns;
} fusedwasi_p2__times;

/* the times to set, from what the file has now; the clock is asked here, and only when the file could be read (FUSEDWASI_P2__E_NO_CLOCK when it cannot be read) */
FUSEDWASI_P2_FN int fusedwasi_p2__fs_times_of(fusedwasi_p2_state *state, const fusedwasi_p2__stat *st, const fusedwasi_p2__times *t, int64_t *a_ns, int64_t *m_ns) {
    uint64_t now = 0;
    if (state->clock(state, 0, &now) != 0) return FUSEDWASI_P2__E_NO_CLOCK;
    *a_ns = fusedwasi_p2__new_time(t->a_tag, t->a_ns, st->atime_ns, (int64_t)now);
    *m_ns = fusedwasi_p2__new_time(t->m_tag, t->m_ns, st->mtime_ns, (int64_t)now);
    return FUSEDWASI_P2__OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2__fs_set_times(fusedwasi_p2_state *state, const fusedwasi_p2__descriptor *d, const fusedwasi_p2__times *t, uint32_t opts) {
    fusedwasi_p2__stat st;
    int64_t a = 0, m = 0;
    int rc = fusedwasi_p2__fs_need(d, d->is_dir ? FUSEDWASI_P2__MUTATE : FUSEDWASI_P2__WRITE, opts);
    if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__sys_fstat(d, &st);
    if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__fs_times_of(state, &st, t, &a, &m);
    return rc == FUSEDWASI_P2__OK ? fusedwasi_p2__sys_set_times_fd(d, a, m) : rc;
}

/* the names of a directory, sorted, without . and .., with their types; the caller frees them (`fusedwasi_p2__names_free`, and `free` of the types) */
FUSEDWASI_P2_FN int fusedwasi_p2__fs_read_directory(const fusedwasi_p2__descriptor *d, char ***names, uint8_t **types, uint32_t *count) {
    if (!d->is_dir) return FUSEDWASI_P2_E_NOT_DIRECTORY;
    return fusedwasi_p2__sys_list(d, names, types, count);
}

FUSEDWASI_P2_FN int fusedwasi_p2__fs_stat_at(const fusedwasi_p2__descriptor *d, const uint8_t *path, uint32_t len, uint32_t path_flags, fusedwasi_p2__stat *out, uint32_t opts) {
    fusedwasi_p2__walk w;
    int rc = fusedwasi_p2__fs_walk(&w, d, path, len, (path_flags & FUSEDWASI_P2__SYMLINK_FOLLOW) != 0, opts);
    if (rc != FUSEDWASI_P2__OK) return rc;
    rc = fusedwasi_p2__sys_lstat(w.dir, w.leaf, out); /* a link at the end was read by the walk if it was to be */
    fusedwasi_p2__walk_close(&w);
    return rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2__fs_set_times_at(fusedwasi_p2_state *state, const fusedwasi_p2__descriptor *d, const uint8_t *path, uint32_t len, uint32_t path_flags,
                                                 const fusedwasi_p2__times *t, uint32_t opts) {
    fusedwasi_p2__walk w;
    int rc = fusedwasi_p2__fs_need(d, FUSEDWASI_P2__MUTATE, opts);
    if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__fs_walk(&w, d, path, len, (path_flags & FUSEDWASI_P2__SYMLINK_FOLLOW) != 0, opts);
    if (rc != FUSEDWASI_P2__OK) return rc;
    fusedwasi_p2__stat st;
    int64_t a = 0, m = 0;
    rc = fusedwasi_p2__sys_lstat(w.dir, w.leaf, &st);
    if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__fs_times_of(state, &st, t, &a, &m);
    if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__sys_set_times(w.dir, w.leaf, a, m);
    fusedwasi_p2__walk_close(&w);
    return rc;
}

/* A name to be made that has a slash after it (a link): the code when it can only be an error; FUSEDWASI_P2__OK when it has none */
FUSEDWASI_P2_FN int fusedwasi_p2__fs_leaf_slash(fusedwasi_p2__dirref parent, const char *leaf) {
    size_t n = strlen(leaf);
    if (n == 0 || leaf[n - 1] != '/') return FUSEDWASI_P2__OK;
    char bare[FUSEDWASI_P2__NAME_MAX + 2];
    memcpy(bare, leaf, n - 1);
    bare[n - 1] = '\0';
    fusedwasi_p2__stat st;
    int rc = fusedwasi_p2__sys_lstat(parent, bare, &st);
    if (rc != FUSEDWASI_P2__OK) return rc;
    return st.type == FUSEDWASI_P2__T_DIRECTORY ? FUSEDWASI_P2_E_EXIST : FUSEDWASI_P2_E_NOT_DIRECTORY;
}

/* The two directories and names of rename and link, walked; both walks are closed by the caller when the answer is FUSEDWASI_P2__OK. `follow` is for the first. */
FUSEDWASI_P2_FN int fusedwasi_p2__fs_pair(const fusedwasi_p2__descriptor *d1, const uint8_t *p1, uint32_t l1, int follow, const fusedwasi_p2__descriptor *d2, const uint8_t *p2, uint32_t l2,
                                         fusedwasi_p2__walk *w1, fusedwasi_p2__walk *w2, uint32_t opts) {
    int rc = fusedwasi_p2__fs_need(d1, FUSEDWASI_P2__MUTATE, opts);
    if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__fs_walk(w1, d1, p1, l1, follow, opts);
    if (rc != FUSEDWASI_P2__OK) return rc;
    rc = fusedwasi_p2__fs_need(d2, FUSEDWASI_P2__MUTATE, opts);
    if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__fs_walk(w2, d2, p2, l2, 0, opts);
    if (rc != FUSEDWASI_P2__OK) {
        fusedwasi_p2__walk_close(w1);
        return rc;
    }
    if (fusedwasi_p2__fs_is_itself(w1, opts) || fusedwasi_p2__fs_is_itself(w2, opts)) { /* a directory itself is neither moved nor made a link */
        rc = fusedwasi_p2__fs_is_itself(w2, opts) ? FUSEDWASI_P2_E_EXIST : FUSEDWASI_P2_E_INVALID;
        fusedwasi_p2__walk_close(w1);
        fusedwasi_p2__walk_close(w2);
        return rc;
    }
    return FUSEDWASI_P2__OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2__fs_link_at(const fusedwasi_p2__descriptor *d, uint32_t old_flags, const uint8_t *old_path, uint32_t old_len, const fusedwasi_p2__descriptor *new_d,
                                            const uint8_t *new_path, uint32_t new_len, uint32_t opts) {
    fusedwasi_p2__walk w1, w2;
    int rc = fusedwasi_p2__fs_pair(d, old_path, old_len, (old_flags & FUSEDWASI_P2__SYMLINK_FOLLOW) != 0, new_d, new_path, new_len, &w1, &w2, opts);
    if (rc != FUSEDWASI_P2__OK) return rc;
    rc = (opts & FUSEDWASI_P2__FS_P1) ? fusedwasi_p2__fs_leaf_slash(w2.dir, w2.leaf) : FUSEDWASI_P2__OK;
    if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__sys_link(w1.dir, w1.leaf, w2.dir, w2.leaf); /* (the walk read a link at the end if it was to be followed) */
    fusedwasi_p2__walk_close(&w1);
    fusedwasi_p2__walk_close(&w2);
    return rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2__fs_rename_at(const fusedwasi_p2__descriptor *d, const uint8_t *old_path, uint32_t old_len, const fusedwasi_p2__descriptor *new_d, const uint8_t *new_path,
                                              uint32_t new_len, uint32_t opts) {
    fusedwasi_p2__walk w1, w2;
    int rc = fusedwasi_p2__fs_pair(d, old_path, old_len, 0, new_d, new_path, new_len, &w1, &w2, opts);
    if (rc != FUSEDWASI_P2__OK) return rc;
    rc = fusedwasi_p2__sys_rename(w1.dir, w1.leaf, w2.dir, w2.leaf);
    fusedwasi_p2__walk_close(&w1);
    fusedwasi_p2__walk_close(&w2);
    return rc;
}

/* open: `made` is the new descriptor with its flags (a directory has what the directory it was opened under has, as in Wasmtime, and not what was asked for) */
FUSEDWASI_P2_FN int fusedwasi_p2__fs_open_at(const fusedwasi_p2__descriptor *d, const uint8_t *path, uint32_t len, uint32_t path_flags, uint32_t open_flags, uint32_t flags,
                                            fusedwasi_p2__descriptor *made, uint32_t opts) {
    if (!d->is_dir) return FUSEDWASI_P2_E_NOT_DIRECTORY;
    if (!(opts & FUSEDWASI_P2__FS_P1)) {
        if ((flags & (FUSEDWASI_P2__WRITE | FUSEDWASI_P2__MUTATE)) && !(d->flags & FUSEDWASI_P2__MUTATE)) return FUSEDWASI_P2_E_READ_ONLY; /* a descriptor cannot hand out more than it has (as the WIT says for open-at) */
        if ((flags & FUSEDWASI_P2__READ) && !(d->flags & FUSEDWASI_P2__READ)) return FUSEDWASI_P2_E_NOT_PERMITTED;
        if ((open_flags & (FUSEDWASI_P2__CREATE | FUSEDWASI_P2__TRUNCATE)) && !(d->flags & FUSEDWASI_P2__MUTATE)) return FUSEDWASI_P2_E_READ_ONLY;
    }
    fusedwasi_p2__walk w;
    int rc = fusedwasi_p2__fs_walk(&w, d, path, len, (path_flags & FUSEDWASI_P2__SYMLINK_FOLLOW) != 0, opts);
    if (rc != FUSEDWASI_P2__OK) return rc;
    memset(made, 0, sizeof *made);
    rc = fusedwasi_p2__sys_open_at(w.dir, w.leaf, open_flags, flags, made);
    fusedwasi_p2__walk_close(&w);
    if (rc == FUSEDWASI_P2__OK) made->flags = made->is_dir ? (d->flags & (FUSEDWASI_P2__READ | FUSEDWASI_P2__MUTATE)) : flags;
    return rc;
}

/* the target of a link in `out` (NUL-terminated, `cap` bytes with the NUL); a rooted target is not shown */
FUSEDWASI_P2_FN int fusedwasi_p2__fs_readlink_at(const fusedwasi_p2__descriptor *d, const uint8_t *path, uint32_t len, char *out, size_t cap, uint32_t opts) {
    fusedwasi_p2__walk w;
    int rc = fusedwasi_p2__fs_walk(&w, d, path, len, 0, opts);
    if (rc != FUSEDWASI_P2__OK) return rc;
    rc = fusedwasi_p2__fs_is_itself(&w, opts) ? FUSEDWASI_P2_E_INVALID : fusedwasi_p2__sys_readlink(w.dir, w.leaf, out, cap);
    fusedwasi_p2__walk_close(&w);
    if (rc == FUSEDWASI_P2__OK && out[0] == '/') return FUSEDWASI_P2_E_NOT_PERMITTED;
    return rc;
}

/* the calls that make or remove a name below a directory */
enum { FUSEDWASI_P2__NAME_MKDIR, FUSEDWASI_P2__NAME_RMDIR, FUSEDWASI_P2__NAME_UNLINK, FUSEDWASI_P2__NAME_SYMLINK };

FUSEDWASI_P2_FN int fusedwasi_p2__fs_name_op(const fusedwasi_p2__descriptor *d, const uint8_t *path, uint32_t len, int op, const char *target, uint32_t opts) {
    fusedwasi_p2__walk w;
    int rc = fusedwasi_p2__fs_need(d, FUSEDWASI_P2__MUTATE, opts);
    if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__fs_walk(&w, d, path, len, 0, opts);
    if (rc != FUSEDWASI_P2__OK) return rc;
    int itself = fusedwasi_p2__fs_is_itself(&w, opts);
    switch (op) {
    case FUSEDWASI_P2__NAME_MKDIR: rc = itself ? FUSEDWASI_P2_E_EXIST : fusedwasi_p2__sys_mkdir(w.dir, w.leaf); break;
    case FUSEDWASI_P2__NAME_RMDIR: rc = itself ? FUSEDWASI_P2_E_INVALID : fusedwasi_p2__sys_rmdir(w.dir, w.leaf); break;
    case FUSEDWASI_P2__NAME_UNLINK:
        rc = itself ? FUSEDWASI_P2_E_INVALID : fusedwasi_p2__sys_unlink(w.dir, w.leaf);
        if ((opts & FUSEDWASI_P2__FS_P1) && rc == FUSEDWASI_P2_E_IS_DIRECTORY) { /* unlink of a directory: the answer of the system (the layer of 0.2 says is-directory for all) */
#if defined(__APPLE__)
            rc = FUSEDWASI_P2_E_NOT_PERMITTED;
#elif defined(_WIN32)
            rc = FUSEDWASI_P2_E_ACCESS;
#endif
        }
        break;
    default:
        rc = itself ? FUSEDWASI_P2_E_EXIST : (opts & FUSEDWASI_P2__FS_P1) ? fusedwasi_p2__fs_leaf_slash(w.dir, w.leaf) : FUSEDWASI_P2__OK;
        if (rc == FUSEDWASI_P2__OK) rc = fusedwasi_p2__sys_symlink(target, w.dir, w.leaf);
        break;
    }
    fusedwasi_p2__walk_close(&w);
    return rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2__fs_create_directory_at(const fusedwasi_p2__descriptor *d, const uint8_t *path, uint32_t len, uint32_t opts) {
    return fusedwasi_p2__fs_name_op(d, path, len, FUSEDWASI_P2__NAME_MKDIR, NULL, opts);
}
FUSEDWASI_P2_FN int fusedwasi_p2__fs_remove_directory_at(const fusedwasi_p2__descriptor *d, const uint8_t *path, uint32_t len, uint32_t opts) {
    return fusedwasi_p2__fs_name_op(d, path, len, FUSEDWASI_P2__NAME_RMDIR, NULL, opts);
}
FUSEDWASI_P2_FN int fusedwasi_p2__fs_unlink_file_at(const fusedwasi_p2__descriptor *d, const uint8_t *path, uint32_t len, uint32_t opts) {
    return fusedwasi_p2__fs_name_op(d, path, len, FUSEDWASI_P2__NAME_UNLINK, NULL, opts);
}
/* `target` is a NUL-terminated string that the caller has looked at (no NUL inside, not rooted) */
FUSEDWASI_P2_FN int fusedwasi_p2__fs_symlink_at(const fusedwasi_p2__descriptor *d, const char *target, const uint8_t *path, uint32_t len, uint32_t opts) {
    return fusedwasi_p2__fs_name_op(d, path, len, FUSEDWASI_P2__NAME_SYMLINK, target, opts);
}

/* ---- the Canonical ABI around them ---------------------------------------------------------------------------------------------------------------------------- */

FUSEDWASI_P2_FN void fusedwasi_p2__fs_fail(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, uint32_t at, int code) {
    if (code == FUSEDWASI_P2__E_NO_CLOCK) g->trap(g, "the clock cannot be read");
    fusedwasi_p2__store_u8(g, comp, retptr, 1);
    fusedwasi_p2__store_u8(g, comp, at, (uint8_t)code);
}

/* result<_, error-code> from a code */
FUSEDWASI_P2_FN void fusedwasi_p2__status(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, int rc) {
    if (rc != FUSEDWASI_P2__OK) {
        fusedwasi_p2__fs_fail(g, comp, retptr, retptr + 1, rc);
        return;
    }
    fusedwasi_p2__ok(g, comp, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_p2__datetime_some(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr, int64_t ns) {
    fusedwasi_p2__store_u8(g, comp, ptr, 1);
    fusedwasi_p2__store_u64(g, comp, ptr + 8, (uint64_t)ns / 1000000000ULL);
    fusedwasi_p2__store_u32(g, comp, ptr + 16, (uint32_t)((uint64_t)ns % 1000000000ULL));
}

FUSEDWASI_P2_FN void fusedwasi_p2__write_stat(fusedwasi_p2_guest *g, uint32_t comp, uint32_t ptr, const fusedwasi_p2__stat *st) {
    fusedwasi_p2__store_u8(g, comp, ptr, st->type);
    fusedwasi_p2__store_u64(g, comp, ptr + 8, st->nlink);
    fusedwasi_p2__store_u64(g, comp, ptr + 16, st->size);
    fusedwasi_p2__datetime_some(g, comp, ptr + 24, st->atime_ns);
    fusedwasi_p2__datetime_some(g, comp, ptr + 48, st->mtime_ns);
    fusedwasi_p2__datetime_some(g, comp, ptr + 72, st->ctime_ns);
}

/* streams over a file: the descriptor is duplicated, so the stream outlives the handle */
typedef struct {
    fusedwasi_p2_stream base;
    int fd;
    uint64_t pos;
    int append;
} fusedwasi_p2__file_stream;

FUSEDWASI_P2_FN void fusedwasi_p2__file_stream_set_failure(fusedwasi_p2_stream *st, int code, const char *what) {
    st->failure = what;
    st->failure_has_code = 1;
    st->failure_code = code;
}

FUSEDWASI_P2_FN int fusedwasi_p2__file_read(fusedwasi_p2_state *state, fusedwasi_p2_stream *st, uint8_t *buf, uint64_t len, uint64_t *got) {
    fusedwasi_p2__file_stream *f = (fusedwasi_p2__file_stream *)st;
    (void)state;
    *got = 0;
    if (len == 0) return FUSEDWASI_P2_STREAM_OK;
    int rc = FUSEDWASI_P2__OK;
    int64_t n = fusedwasi_p2__sys_pread(f->fd, buf, (size_t)len, f->pos, &rc);
    if (n < 0) {
        fusedwasi_p2__file_stream_set_failure(st, rc, "read failed");
        return FUSEDWASI_P2_STREAM_FAILED;
    }
    if (n == 0) return FUSEDWASI_P2_STREAM_CLOSED;
    f->pos += (uint64_t)n;
    *got = (uint64_t)n;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN uint64_t fusedwasi_p2__file_check_write(fusedwasi_p2_state *state, fusedwasi_p2_stream *st, int *status) {
    (void)state, (void)st;
    *status = FUSEDWASI_P2_STREAM_OK;
    return FUSEDWASI_P2__STREAM_CHUNK;
}

FUSEDWASI_P2_FN int fusedwasi_p2__file_write(fusedwasi_p2_state *state, fusedwasi_p2_stream *st, const uint8_t *data, uint64_t len) {
    fusedwasi_p2__file_stream *f = (fusedwasi_p2__file_stream *)st;
    (void)state;
    uint64_t at = f->pos;
    if (f->append) {
        int rc = fusedwasi_p2__sys_size(f->fd, &at);
        if (rc != FUSEDWASI_P2__OK) {
            fusedwasi_p2__file_stream_set_failure(st, rc, "write failed");
            return FUSEDWASI_P2_STREAM_FAILED;
        }
    }
    uint64_t done = 0;
    while (done < len) {
        int rc = FUSEDWASI_P2__OK;
        int64_t n = fusedwasi_p2__sys_pwrite(f->fd, data + done, (size_t)(len - done), at + done, &rc);
        if (n < 0) {
            fusedwasi_p2__file_stream_set_failure(st, rc, "write failed");
            return FUSEDWASI_P2_STREAM_FAILED;
        }
        done += (uint64_t)n;
    }
    if (!f->append) f->pos += len;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2__file_flush(fusedwasi_p2_state *state, fusedwasi_p2_stream *st) {
    (void)state, (void)st;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN void fusedwasi_p2__file_stream_destroy(fusedwasi_p2_state *state, fusedwasi_p2_stream *st) {
    fusedwasi_p2__sys_closefd(((fusedwasi_p2__file_stream *)st)->fd);
    fusedwasi_p2__stream_free(state, st);
}

/* a handle on a new stream over `fd` (dup-ed); `output`: 0 reads from `pos`, 1 writes at `pos`, 2 appends */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2__file_stream_new(fusedwasi_p2_state *state, int fd, uint64_t pos, int output) {
    fusedwasi_p2__file_stream *f = (fusedwasi_p2__file_stream *)calloc(1, sizeof *f);
    if (!f) return 0;
    f->fd = fd >= 0 ? fusedwasi_p2__sys_dup(fd) : -1;
    f->pos = pos;
    f->append = output == 2;
    uint32_t handle;
    f->base.ready = fusedwasi_p2__always_ready;
    f->base.destroy = fusedwasi_p2__file_stream_destroy;
    if (output) {
        f->base.check_write = fusedwasi_p2__file_check_write;
        f->base.write = fusedwasi_p2__file_write;
        f->base.flush = fusedwasi_p2__file_flush;
        handle = fusedwasi_p2_output_stream_add(state, &f->base);
    } else {
        f->base.read = fusedwasi_p2__file_read;
        handle = fusedwasi_p2_input_stream_add(state, &f->base);
    }
    if (!handle) fusedwasi_p2__file_stream_destroy(state, &f->base); /* closes the dup-ed descriptor */
    return handle;
}

/* preopens */

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_preopens_get_directories(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr) {
    uint32_t ptr = fusedwasi_p2__alloc(g, comp, 4, 12 * g->state->preopen_count);
    for (uint32_t i = 0; i < g->state->preopen_count; i++) {
        fusedwasi_p2__descriptor made;
        if (fusedwasi_p2__sys_open_directory(g->state->preopens[i].path, &made) != FUSEDWASI_P2__OK) g->trap(g, "cannot open a preopened directory");
        uint32_t handle = fusedwasi_p2__handle_or_trap(g, fusedwasi_p2__descriptor_new(g->state, &made, FUSEDWASI_P2__READ | (g->state->preopens[i].writable ? FUSEDWASI_P2__MUTATE : 0)));
        fusedwasi_p2__store_u32(g, comp, ptr + 12 * i, handle);
        fusedwasi_p2__string(g, comp, g->state->preopens[i].name, ptr + 12 * i + 4);
    }
    fusedwasi_p2__store_u32(g, comp, retptr, ptr);
    fusedwasi_p2__store_u32(g, comp, retptr + 4, g->state->preopen_count);
}

/* the descriptor */

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2__descriptor_get(g, handle);
    fusedwasi_p2__table_drop(g, handle, FUSEDWASI_P2_KIND_DESCRIPTOR);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_directory_entry_stream_drop(fusedwasi_p2_guest *g, uint32_t handle) {
    fusedwasi_p2__table_get(g, handle, FUSEDWASI_P2_KIND_DIRECTORY_ENTRY_STREAM);
    fusedwasi_p2__table_drop(g, handle, FUSEDWASI_P2_KIND_DIRECTORY_ENTRY_STREAM);
}

/* result<handle, error-code>: a code, or a handle */
FUSEDWASI_P2_FN void fusedwasi_p2__handle_result(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, int rc, uint32_t handle) {
    if (rc != FUSEDWASI_P2__OK) {
        fusedwasi_p2__fs_fail(g, comp, retptr, retptr + 4, rc);
        return;
    }
    fusedwasi_p2__ok(g, comp, retptr);
    fusedwasi_p2__store_u32(g, comp, retptr + 4, handle);
}

FUSEDWASI_P2_FN void fusedwasi_p2__stream_of(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, uint32_t self, uint64_t pos, int output) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    int rc = fusedwasi_p2__need(d, output ? FUSEDWASI_P2__WRITE : FUSEDWASI_P2__READ);
    fusedwasi_p2__handle_result(g, comp, retptr, rc, rc == FUSEDWASI_P2__OK ? fusedwasi_p2__handle_or_trap(g, fusedwasi_p2__file_stream_new(g->state, d->fd, pos, output)) : 0);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_read_via_stream(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t offset, uint32_t retptr) {
    fusedwasi_p2__stream_of(g, comp, retptr, self, offset, 0);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_write_via_stream(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t offset, uint32_t retptr) {
    fusedwasi_p2__stream_of(g, comp, retptr, self, offset, 1);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_append_via_stream(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2__stream_of(g, comp, retptr, self, 0, 2);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_advise(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t offset, uint64_t length, uint32_t advice,
                                                        uint32_t retptr) {
    (void)offset, (void)length, (void)advice;
    fusedwasi_p2__descriptor_get(g, self); /* only a hint: accepted, and nothing done */
    fusedwasi_p2__status(g, comp, retptr, FUSEDWASI_P2__OK);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_sync_data(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2__status(g, comp, retptr, fusedwasi_p2__fs_sync(fusedwasi_p2__descriptor_get(g, self), 1));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_sync(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2__status(g, comp, retptr, fusedwasi_p2__fs_sync(fusedwasi_p2__descriptor_get(g, self), 0));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_get_flags(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    fusedwasi_p2__ok(g, comp, retptr);
    fusedwasi_p2__store_u8(g, comp, retptr + 1, (uint8_t)d->flags);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_get_type(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    fusedwasi_p2__stat st;
    int rc = fusedwasi_p2__fs_stat(d, &st);
    if (rc != FUSEDWASI_P2__OK) {
        fusedwasi_p2__fs_fail(g, comp, retptr, retptr + 1, rc);
        return;
    }
    fusedwasi_p2__ok(g, comp, retptr);
    fusedwasi_p2__store_u8(g, comp, retptr + 1, st.type);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_set_size(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t size, uint32_t retptr) {
    fusedwasi_p2__status(g, comp, retptr, fusedwasi_p2__fs_set_size(fusedwasi_p2__descriptor_get(g, self), size, 0));
}

/* the times of a call: new-timestamp is 0 no-change, 1 now, 2 timestamp */
FUSEDWASI_P2_FN void fusedwasi_p2__times_of_abi(fusedwasi_p2__times *t, uint32_t a_tag, uint64_t a_seconds, uint32_t a_nanos, uint32_t m_tag, uint64_t m_seconds, uint32_t m_nanos) {
    t->a_tag = a_tag, t->m_tag = m_tag;
    t->a_ns = (int64_t)a_seconds * 1000000000LL + a_nanos;
    t->m_ns = (int64_t)m_seconds * 1000000000LL + m_nanos;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_set_times(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t a_tag, uint64_t a_seconds, uint32_t a_nanos,
                                                           uint32_t m_tag, uint64_t m_seconds, uint32_t m_nanos, uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    fusedwasi_p2__times t;
    fusedwasi_p2__times_of_abi(&t, a_tag, a_seconds, a_nanos, m_tag, m_seconds, m_nanos);
    fusedwasi_p2__status(g, comp, retptr, fusedwasi_p2__fs_set_times(g->state, d, &t, 0));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_read(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint64_t length, uint64_t offset, uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    uint64_t want = length < FUSEDWASI_P2__READ_CAP ? length : FUSEDWASI_P2__READ_CAP;
    uint8_t *buf = (uint8_t *)malloc(want ? (size_t)want : 1);
    if (!buf) g->trap(g, "out of memory");
    uint64_t n = 0;
    int rc = fusedwasi_p2__fs_read(d, buf, want, offset, &n, 0);
    if (rc != FUSEDWASI_P2__OK) {
        free(buf);
        fusedwasi_p2__fs_fail(g, comp, retptr, retptr + 4, rc);
        return;
    }
    uint32_t ptr = fusedwasi_p2__alloc(g, comp, 1, (uint32_t)n);
    if (n) memcpy(fusedwasi_p2__mem(g, comp, ptr, (uint32_t)n), buf, (size_t)n);
    free(buf);
    fusedwasi_p2__ok(g, comp, retptr);
    fusedwasi_p2__store_u32(g, comp, retptr + 4, ptr);
    fusedwasi_p2__store_u32(g, comp, retptr + 8, (uint32_t)n);
    fusedwasi_p2__store_u8(g, comp, retptr + 12, (uint8_t)(n == 0)); /* end of file is when nothing was read (as in Wasmtime) */
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_write(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t length, uint64_t offset,
                                                       uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    const uint8_t *data = fusedwasi_p2__mem(g, comp, ptr, length);
    uint64_t done = 0;
    int rc = fusedwasi_p2__fs_write(d, data, length, offset, &done, 0);
    if (rc != FUSEDWASI_P2__OK) {
        fusedwasi_p2__fs_fail(g, comp, retptr, retptr + 8, rc);
        return;
    }
    fusedwasi_p2__ok(g, comp, retptr);
    fusedwasi_p2__store_u64(g, comp, retptr + 8, length);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_read_directory(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    char **names = NULL;
    uint8_t *types = NULL;
    uint32_t count = 0;
    int rc = fusedwasi_p2__fs_read_directory(d, &names, &types, &count);
    if (rc != FUSEDWASI_P2__OK) {
        fusedwasi_p2__handle_result(g, comp, retptr, rc, 0);
        return;
    }
    fusedwasi_p2__dir_entries *ent = (fusedwasi_p2__dir_entries *)calloc(1, sizeof *ent);
    if (!ent) g->trap(g, "out of memory");
    ent->count = count, ent->names = names, ent->types = types;
    fusedwasi_p2__handle_result(g, comp, retptr, FUSEDWASI_P2__OK, fusedwasi_p2__table_new(g, FUSEDWASI_P2_KIND_DIRECTORY_ENTRY_STREAM, ent, fusedwasi_p2__dir_entries_destroy));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_directory_entry_stream_read_directory_entry(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2__dir_entries *e = (fusedwasi_p2__dir_entries *)fusedwasi_p2__table_get(g, self, FUSEDWASI_P2_KIND_DIRECTORY_ENTRY_STREAM)->obj;
    fusedwasi_p2__ok(g, comp, retptr);
    if (e->pos >= e->count) {
        fusedwasi_p2__store_u8(g, comp, retptr + 4, 0);
        return;
    }
    uint32_t i = e->pos++;
    fusedwasi_p2__store_u8(g, comp, retptr + 4, 1);
    fusedwasi_p2__store_u8(g, comp, retptr + 8, e->types[i]);
    fusedwasi_p2__string(g, comp, e->names[i], retptr + 12);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_stat(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    fusedwasi_p2__stat st;
    int rc = fusedwasi_p2__fs_stat(d, &st);
    if (rc != FUSEDWASI_P2__OK) {
        fusedwasi_p2__fs_fail(g, comp, retptr, retptr + 8, rc);
        return;
    }
    fusedwasi_p2__ok(g, comp, retptr);
    fusedwasi_p2__write_stat(g, comp, retptr + 8, &st);
}

FUSEDWASI_P2_FN int fusedwasi_p2__stat_at_rc(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t path_flags, uint32_t ptr, uint32_t length, fusedwasi_p2__stat *st) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    const uint8_t *path = fusedwasi_p2__mem(g, comp, ptr, length);
    return fusedwasi_p2__fs_stat_at(d, path, length, path_flags, st, 0);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_stat_at(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t path_flags, uint32_t ptr, uint32_t length,
                                                         uint32_t retptr) {
    fusedwasi_p2__stat st;
    int rc = fusedwasi_p2__stat_at_rc(g, comp, self, path_flags, ptr, length, &st);
    if (rc != FUSEDWASI_P2__OK) {
        fusedwasi_p2__fs_fail(g, comp, retptr, retptr + 8, rc);
        return;
    }
    fusedwasi_p2__ok(g, comp, retptr);
    fusedwasi_p2__write_stat(g, comp, retptr + 8, &st);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_set_times_at(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t path_flags, uint32_t ptr, uint32_t length,
                                                              uint32_t a_tag, uint64_t a_seconds, uint32_t a_nanos, uint32_t m_tag, uint64_t m_seconds,
                                                              uint32_t m_nanos, uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    const uint8_t *path = fusedwasi_p2__mem(g, comp, ptr, length);
    fusedwasi_p2__times t;
    fusedwasi_p2__times_of_abi(&t, a_tag, a_seconds, a_nanos, m_tag, m_seconds, m_nanos);
    fusedwasi_p2__status(g, comp, retptr, fusedwasi_p2__fs_set_times_at(g->state, d, path, length, path_flags, &t, 0));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_link_at(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t old_flags, uint32_t old_ptr, uint32_t old_len,
                                                         uint32_t new_dir, uint32_t new_ptr, uint32_t new_len, uint32_t retptr) {
    fusedwasi_p2__descriptor *n = fusedwasi_p2__descriptor_get(g, new_dir), *d = fusedwasi_p2__descriptor_get(g, self);
    const uint8_t *old_path = fusedwasi_p2__mem(g, comp, old_ptr, old_len), *new_path = fusedwasi_p2__mem(g, comp, new_ptr, new_len);
    fusedwasi_p2__status(g, comp, retptr, fusedwasi_p2__fs_link_at(d, old_flags, old_path, old_len, n, new_path, new_len, 0));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_open_at(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t path_flags, uint32_t ptr, uint32_t length,
                                                         uint32_t open_flags, uint32_t flags, uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    const uint8_t *path = fusedwasi_p2__mem(g, comp, ptr, length);
    fusedwasi_p2__descriptor made;
    uint32_t handle = 0;
    int rc = fusedwasi_p2__fs_open_at(d, path, length, path_flags, open_flags, flags, &made, 0);
    if (rc == FUSEDWASI_P2__OK) {
        handle = fusedwasi_p2__descriptor_new(g->state, &made, made.flags);
        if (!handle) fusedwasi_p2__sys_close(&made);
        handle = fusedwasi_p2__handle_or_trap(g, handle);
    }
    fusedwasi_p2__handle_result(g, comp, retptr, rc, handle);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_readlink_at(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t length, uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    const uint8_t *path = fusedwasi_p2__mem(g, comp, ptr, length);
    char target[FUSEDWASI_P2__PATH_MAX];
    int rc = fusedwasi_p2__fs_readlink_at(d, path, length, target, sizeof target, 0);
    if (rc != FUSEDWASI_P2__OK) {
        fusedwasi_p2__fs_fail(g, comp, retptr, retptr + 4, rc);
        return;
    }
    fusedwasi_p2__ok(g, comp, retptr);
    fusedwasi_p2__string(g, comp, target, retptr + 4);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_create_directory_at(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t length,
                                                                     uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    fusedwasi_p2__status(g, comp, retptr, fusedwasi_p2__fs_create_directory_at(d, fusedwasi_p2__mem(g, comp, ptr, length), length, 0));
}
FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_remove_directory_at(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t length,
                                                                     uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    fusedwasi_p2__status(g, comp, retptr, fusedwasi_p2__fs_remove_directory_at(d, fusedwasi_p2__mem(g, comp, ptr, length), length, 0));
}
FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_unlink_file_at(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t length, uint32_t retptr) {
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    fusedwasi_p2__status(g, comp, retptr, fusedwasi_p2__fs_unlink_file_at(d, fusedwasi_p2__mem(g, comp, ptr, length), length, 0));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_rename_at(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t old_ptr, uint32_t old_len, uint32_t new_dir,
                                                           uint32_t new_ptr, uint32_t new_len, uint32_t retptr) {
    fusedwasi_p2__descriptor *n = fusedwasi_p2__descriptor_get(g, new_dir), *d = fusedwasi_p2__descriptor_get(g, self);
    const uint8_t *old_path = fusedwasi_p2__mem(g, comp, old_ptr, old_len), *new_path = fusedwasi_p2__mem(g, comp, new_ptr, new_len);
    fusedwasi_p2__status(g, comp, retptr, fusedwasi_p2__fs_rename_at(d, old_path, old_len, n, new_path, new_len, 0));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_symlink_at(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t old_ptr, uint32_t old_len, uint32_t new_ptr,
                                                            uint32_t new_len, uint32_t retptr) {
    const uint8_t *target = fusedwasi_p2__mem(g, comp, old_ptr, old_len);
    if (memchr(target, 0, old_len) || (old_len && target[0] == '/')) {
        fusedwasi_p2__descriptor_get(g, self);
        fusedwasi_p2__status(g, comp, retptr, memchr(target, 0, old_len) ? FUSEDWASI_P2_E_INVALID : FUSEDWASI_P2_E_NOT_PERMITTED);
        return;
    }
    fusedwasi_p2__descriptor *d = fusedwasi_p2__descriptor_get(g, self);
    const uint8_t *path = fusedwasi_p2__mem(g, comp, new_ptr, new_len);
    char *copy = (char *)malloc((size_t)old_len + 1);
    if (!copy) g->trap(g, "out of memory");
    memcpy(copy, target, old_len);
    copy[old_len] = '\0';
    int rc = fusedwasi_p2__fs_symlink_at(d, copy, path, new_len, 0);
    free(copy);
    fusedwasi_p2__status(g, comp, retptr, rc);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_filesystem_types_descriptor_is_same_object(fusedwasi_p2_guest *g, uint32_t self, uint32_t other) {
    fusedwasi_p2__stat a, b;
    int r1 = fusedwasi_p2__sys_fstat(fusedwasi_p2__descriptor_get(g, self), &a), r2 = fusedwasi_p2__sys_fstat(fusedwasi_p2__descriptor_get(g, other), &b);
    return (uint32_t)(r1 == FUSEDWASI_P2__OK && r2 == FUSEDWASI_P2__OK && a.dev == b.dev && a.ino == b.ino);
}

FUSEDWASI_P2_FN uint64_t fusedwasi_p2__fnv(const uint64_t *values, int n) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (int i = 0; i < n; i++)
        for (int b = 0; b < 8; b++) h = (h ^ ((values[i] >> (8 * b)) & 0xFF)) * 0x100000001B3ULL;
    return h;
}

FUSEDWASI_P2_FN void fusedwasi_p2__hash_result(fusedwasi_p2_guest *g, uint32_t comp, uint32_t retptr, int rc, const fusedwasi_p2__stat *st) {
    if (rc != FUSEDWASI_P2__OK) {
        fusedwasi_p2__fs_fail(g, comp, retptr, retptr + 8, rc);
        return;
    }
    fusedwasi_p2__ok(g, comp, retptr);
    /* `upper` names the object (device, inode); `lower` also takes in what changes when it is modified (size, modification time): FNV-1a 64 as the Python twin */
    uint64_t lower[4] = {st->size, (uint64_t)st->mtime_ns, st->dev, st->ino};
    uint64_t upper[2] = {st->dev, st->ino};
    fusedwasi_p2__store_u64(g, comp, retptr + 8, fusedwasi_p2__fnv(lower, 4));
    fusedwasi_p2__store_u64(g, comp, retptr + 16, fusedwasi_p2__fnv(upper, 2));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_metadata_hash(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2__stat st;
    fusedwasi_p2__hash_result(g, comp, retptr, fusedwasi_p2__sys_fstat(fusedwasi_p2__descriptor_get(g, self), &st), &st);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_metadata_hash_at(fusedwasi_p2_guest *g, uint32_t comp, uint32_t self, uint32_t path_flags, uint32_t ptr,
                                                                  uint32_t length, uint32_t retptr) {
    fusedwasi_p2__stat st;
    fusedwasi_p2__hash_result(g, comp, retptr, fusedwasi_p2__stat_at_rc(g, comp, self, path_flags, ptr, length, &st), &st);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_filesystem_error_code(fusedwasi_p2_guest *g, uint32_t comp, uint32_t err, uint32_t retptr) {
    fusedwasi_p2__error *e = (fusedwasi_p2__error *)fusedwasi_p2__table_get(g, err, FUSEDWASI_P2_KIND_ERROR)->obj;
    if (e->code < 0) {
        fusedwasi_p2__store_u8(g, comp, retptr, 0);
        return;
    }
    fusedwasi_p2__store_u8(g, comp, retptr, 1);
    fusedwasi_p2__store_u8(g, comp, retptr + 1, (uint8_t)e->code);
}

#endif /* FUSEDWASI_P2__FILESYSTEM_H */
