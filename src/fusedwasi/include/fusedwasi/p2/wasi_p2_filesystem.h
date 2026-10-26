/* wasi:filesystem (32 imports): preopens and types (the twin of p2/wasi_p2_filesystem.py, where the confinement of paths, the rights and the layouts are described).
 * POSIX (openat and the rest of the *at calls: wasi_p2_fs_posix.h) and Windows (paths below a directory: wasi_p2_fs_win32.h). The state keeps the preopens:
 * `s->preopens`, a list of {path, name, writable} owned by the caller. Every function that can fail with an error-code writes it in the result; a bad handle or a pointer
 * outside the memory is a trap. */
#ifndef FUSEDWASI_P2_FILESYSTEM_H
#define FUSEDWASI_P2_FILESYSTEM_H

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
#define FUSEDWASI_P2_OK (-1) /* what the functions of this file return when nothing failed; an error-code otherwise */

/* descriptor-flags, path-flags, open-flags */
enum { FUSEDWASI_P2_READ = 1, FUSEDWASI_P2_WRITE = 2, FUSEDWASI_P2_FILE_SYNC = 4, FUSEDWASI_P2_DATA_SYNC = 8, FUSEDWASI_P2_REQUESTED_SYNC = 16, FUSEDWASI_P2_MUTATE = 32 };
enum { FUSEDWASI_P2_SYMLINK_FOLLOW = 1 };
enum { FUSEDWASI_P2_CREATE = 1, FUSEDWASI_P2_DIRECTORY = 2, FUSEDWASI_P2_EXCLUSIVE = 4, FUSEDWASI_P2_TRUNCATE = 8 };
/* descriptor-type */
enum { FUSEDWASI_P2_T_UNKNOWN, FUSEDWASI_P2_T_BLOCK, FUSEDWASI_P2_T_CHAR, FUSEDWASI_P2_T_DIRECTORY, FUSEDWASI_P2_T_FIFO, FUSEDWASI_P2_T_SYMLINK, FUSEDWASI_P2_T_REGULAR, FUSEDWASI_P2_T_SOCKET };

#define FUSEDWASI_P2_NAME_MAX 255
#define FUSEDWASI_P2_MAX_DEPTH 128
#define FUSEDWASI_P2_MAX_LINKS 40
#define FUSEDWASI_P2_PATH_MAX 8192
#define FUSEDWASI_P2_STREAM_CHUNK 65536u

/* what the system says of a file (for both systems): times in nanoseconds since 1970 */
typedef struct {
    uint8_t type;
    uint64_t nlink, size, dev, ino;
    int64_t atime_ns, mtime_ns, ctime_ns;
} fusedwasi_p2_stat;

typedef struct {
    int fd; /* POSIX: the descriptor; Windows: the C-library descriptor of a file, -1 for a directory */
    uint32_t flags;
    int is_dir;
    char *path; /* Windows: where it is, as UTF-8 with the \\?\ prefix (malloc'd); POSIX: NULL */
} fusedwasi_p2_descriptor;

typedef struct {
    uint32_t count, pos;
    uint8_t *types;
    char **names;
} fusedwasi_p2_dir_entries;

FUSEDWASI_P2_FN int fusedwasi_p2_errno_code(int e) {
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
#if defined(ENOTSUP) && defined(EOPNOTSUPP) && ENOTSUP != EOPNOTSUPP
    case ENOTSUP: return FUSEDWASI_P2_E_UNSUPPORTED;
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
FUSEDWASI_P2_FN int fusedwasi_p2_fail(void) { return fusedwasi_p2_errno_code(errno); }

#ifdef _WIN32
#include "wasi_p2_fs_win32.h"
#else
#include "wasi_p2_fs_posix.h"
#endif

/* the descriptors and what they stand for */

FUSEDWASI_P2_FN void fusedwasi_p2_descriptor_destroy(fusedwasi_p2_state *s, void *obj) {
    fusedwasi_p2_descriptor *d = (fusedwasi_p2_descriptor *)obj;
    (void)s;
    fusedwasi_p2_sys_close(d);
    free(d);
}

FUSEDWASI_P2_FN void fusedwasi_p2_dir_entries_destroy(fusedwasi_p2_state *s, void *obj) {
    fusedwasi_p2_dir_entries *e = (fusedwasi_p2_dir_entries *)obj;
    (void)s;
    for (uint32_t i = 0; i < e->count; i++) free(e->names[i]);
    free(e->names);
    free(e->types);
    free(e);
}

FUSEDWASI_P2_FN fusedwasi_p2_descriptor *fusedwasi_p2_descriptor_get(fusedwasi_p2_state *s, uint32_t handle) {
    return (fusedwasi_p2_descriptor *)fusedwasi_p2_table_get(s, handle, FUSEDWASI_P2_KIND_DESCRIPTOR)->obj;
}

/* the rights: FUSEDWASI_P2_OK, or the error-code that the absence of the flag is */
FUSEDWASI_P2_FN int fusedwasi_p2_need(const fusedwasi_p2_descriptor *d, uint32_t flag) {
    if (d->flags & flag) return FUSEDWASI_P2_OK;
    return flag == FUSEDWASI_P2_MUTATE ? FUSEDWASI_P2_E_READ_ONLY : FUSEDWASI_P2_E_BAD_DESCRIPTOR; /* `mutate-directory` unset: read-only (the WIT says so) */
}

/* a handle on a descriptor that the system call filled (`d` is copied) */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2_descriptor_new(fusedwasi_p2_state *s, const fusedwasi_p2_descriptor *made, uint32_t flags) {
    fusedwasi_p2_descriptor *d = (fusedwasi_p2_descriptor *)malloc(sizeof *d);
    if (!d) s->trap(s, "out of memory");
    *d = *made;
    d->flags = flags;
    return fusedwasi_p2_table_new(s, FUSEDWASI_P2_KIND_DESCRIPTOR, d, fusedwasi_p2_descriptor_destroy);
}

/* results */

FUSEDWASI_P2_FN void fusedwasi_p2_fs_fail(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr, uint32_t at, int code) {
    fusedwasi_p2_store_u8(s, comp, retptr, 1);
    fusedwasi_p2_store_u8(s, comp, at, (uint8_t)code);
}

/* result<_, error-code> from a code */
FUSEDWASI_P2_FN void fusedwasi_p2_status(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr, int rc) {
    if (rc != FUSEDWASI_P2_OK) {
        fusedwasi_p2_fs_fail(s, comp, retptr, retptr + 1, rc);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
}

FUSEDWASI_P2_FN void fusedwasi_p2_datetime_some(fusedwasi_p2_state *s, uint32_t comp, uint32_t ptr, int64_t ns) {
    fusedwasi_p2_store_u8(s, comp, ptr, 1);
    fusedwasi_p2_store_u64(s, comp, ptr + 8, (uint64_t)ns / 1000000000ULL);
    fusedwasi_p2_store_u32(s, comp, ptr + 16, (uint32_t)((uint64_t)ns % 1000000000ULL));
}

FUSEDWASI_P2_FN void fusedwasi_p2_write_stat(fusedwasi_p2_state *s, uint32_t comp, uint32_t ptr, const fusedwasi_p2_stat *st) {
    fusedwasi_p2_store_u8(s, comp, ptr, st->type);
    fusedwasi_p2_store_u64(s, comp, ptr + 8, st->nlink);
    fusedwasi_p2_store_u64(s, comp, ptr + 16, st->size);
    fusedwasi_p2_datetime_some(s, comp, ptr + 24, st->atime_ns);
    fusedwasi_p2_datetime_some(s, comp, ptr + 48, st->mtime_ns);
    fusedwasi_p2_datetime_some(s, comp, ptr + 72, st->ctime_ns);
}

/* streams over a file: the descriptor is duplicated, so the stream outlives the handle */
typedef struct {
    fusedwasi_p2_stream base;
    int fd;
    uint64_t pos;
    int append;
} fusedwasi_p2_file_stream;

FUSEDWASI_P2_FN void fusedwasi_p2_file_stream_set_failure(fusedwasi_p2_stream *st, int code, const char *what) {
    st->failure = what;
    st->failure_has_code = 1;
    st->failure_code = code;
}

FUSEDWASI_P2_FN int fusedwasi_p2_file_read(fusedwasi_p2_state *s, fusedwasi_p2_stream *st, uint8_t *buf, uint64_t len, uint64_t *got) {
    fusedwasi_p2_file_stream *f = (fusedwasi_p2_file_stream *)st;
    (void)s;
    *got = 0;
    if (len == 0) return FUSEDWASI_P2_STREAM_OK;
    int rc = FUSEDWASI_P2_OK;
    int64_t n = fusedwasi_p2_sys_pread(f->fd, buf, (size_t)len, f->pos, &rc);
    if (n < 0) {
        fusedwasi_p2_file_stream_set_failure(st, rc, "read failed");
        return FUSEDWASI_P2_STREAM_FAILED;
    }
    if (n == 0) return FUSEDWASI_P2_STREAM_CLOSED;
    f->pos += (uint64_t)n;
    *got = (uint64_t)n;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN uint64_t fusedwasi_p2_file_check_write(fusedwasi_p2_state *s, fusedwasi_p2_stream *st, int *status) {
    (void)s, (void)st;
    *status = FUSEDWASI_P2_STREAM_OK;
    return FUSEDWASI_P2_STREAM_CHUNK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_file_write(fusedwasi_p2_state *s, fusedwasi_p2_stream *st, const uint8_t *data, uint64_t len) {
    fusedwasi_p2_file_stream *f = (fusedwasi_p2_file_stream *)st;
    (void)s;
    uint64_t at = f->pos;
    if (f->append) {
        int rc = fusedwasi_p2_sys_size(f->fd, &at);
        if (rc != FUSEDWASI_P2_OK) {
            fusedwasi_p2_file_stream_set_failure(st, rc, "write failed");
            return FUSEDWASI_P2_STREAM_FAILED;
        }
    }
    uint64_t done = 0;
    while (done < len) {
        int rc = FUSEDWASI_P2_OK;
        int64_t n = fusedwasi_p2_sys_pwrite(f->fd, data + done, (size_t)(len - done), at + done, &rc);
        if (n < 0) {
            fusedwasi_p2_file_stream_set_failure(st, rc, "write failed");
            return FUSEDWASI_P2_STREAM_FAILED;
        }
        done += (uint64_t)n;
    }
    if (!f->append) f->pos += len;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_file_flush(fusedwasi_p2_state *s, fusedwasi_p2_stream *st) {
    (void)s, (void)st;
    return FUSEDWASI_P2_STREAM_OK;
}

FUSEDWASI_P2_FN void fusedwasi_p2_file_stream_destroy(fusedwasi_p2_state *s, fusedwasi_p2_stream *st) {
    fusedwasi_p2_sys_closefd(((fusedwasi_p2_file_stream *)st)->fd);
    fusedwasi_p2_stream_free(s, st);
}

/* a handle on a new stream over `fd` (dup-ed); `output`: 0 reads from `pos`, 1 writes at `pos`, 2 appends */
FUSEDWASI_P2_FN uint32_t fusedwasi_p2_file_stream_new(fusedwasi_p2_state *s, int fd, uint64_t pos, int output) {
    fusedwasi_p2_file_stream *f = (fusedwasi_p2_file_stream *)calloc(1, sizeof *f);
    if (!f) s->trap(s, "out of memory");
    f->fd = fd >= 0 ? fusedwasi_p2_sys_dup(fd) : -1;
    f->pos = pos;
    f->append = output == 2;
    f->base.ready = fusedwasi_p2_always_ready;
    f->base.destroy = fusedwasi_p2_file_stream_destroy;
    if (output) {
        f->base.check_write = fusedwasi_p2_file_check_write;
        f->base.write = fusedwasi_p2_file_write;
        f->base.flush = fusedwasi_p2_file_flush;
        return fusedwasi_p2_output_stream_new(s, &f->base);
    }
    f->base.read = fusedwasi_p2_file_read;
    return fusedwasi_p2_input_stream_new(s, &f->base);
}

/* preopens */

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_preopens_get_directories(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr) {
    uint32_t ptr = fusedwasi_p2_alloc(s, comp, 4, 12 * s->preopen_count);
    for (uint32_t i = 0; i < s->preopen_count; i++) {
        fusedwasi_p2_descriptor made;
        if (fusedwasi_p2_sys_open_directory(s->preopens[i].path, &made) != FUSEDWASI_P2_OK) s->trap(s, "cannot open a preopened directory");
        uint32_t handle = fusedwasi_p2_descriptor_new(s, &made, FUSEDWASI_P2_READ | (s->preopens[i].writable ? FUSEDWASI_P2_MUTATE : 0));
        fusedwasi_p2_store_u32(s, comp, ptr + 12 * i, handle);
        fusedwasi_p2_string(s, comp, s->preopens[i].name, ptr + 12 * i + 4);
    }
    fusedwasi_p2_store_u32(s, comp, retptr, ptr);
    fusedwasi_p2_store_u32(s, comp, retptr + 4, s->preopen_count);
}

/* the descriptor */

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_drop(fusedwasi_p2_state *s, uint32_t handle) {
    fusedwasi_p2_descriptor_get(s, handle);
    fusedwasi_p2_table_drop(s, handle, FUSEDWASI_P2_KIND_DESCRIPTOR);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_directory_entry_stream_drop(fusedwasi_p2_state *s, uint32_t handle) {
    fusedwasi_p2_table_get(s, handle, FUSEDWASI_P2_KIND_DIRECTORY_ENTRY_STREAM);
    fusedwasi_p2_table_drop(s, handle, FUSEDWASI_P2_KIND_DIRECTORY_ENTRY_STREAM);
}

/* result<handle, error-code>: a code, or a handle */
FUSEDWASI_P2_FN void fusedwasi_p2_handle_result(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr, int rc, uint32_t handle) {
    if (rc != FUSEDWASI_P2_OK) {
        fusedwasi_p2_fs_fail(s, comp, retptr, retptr + 4, rc);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_store_u32(s, comp, retptr + 4, handle);
}

FUSEDWASI_P2_FN void fusedwasi_p2_stream_of(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr, uint32_t self, uint64_t pos, int output) {
    fusedwasi_p2_descriptor *d = fusedwasi_p2_descriptor_get(s, self);
    int rc = fusedwasi_p2_need(d, output ? FUSEDWASI_P2_WRITE : FUSEDWASI_P2_READ);
    fusedwasi_p2_handle_result(s, comp, retptr, rc, rc == FUSEDWASI_P2_OK ? fusedwasi_p2_file_stream_new(s, d->fd, pos, output) : 0);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_read_via_stream(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t offset, uint32_t retptr) {
    fusedwasi_p2_stream_of(s, comp, retptr, self, offset, 0);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_write_via_stream(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t offset, uint32_t retptr) {
    fusedwasi_p2_stream_of(s, comp, retptr, self, offset, 1);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_append_via_stream(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_stream_of(s, comp, retptr, self, 0, 2);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_advise(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t offset, uint64_t length, uint32_t advice,
                                                        uint32_t retptr) {
    (void)offset, (void)length, (void)advice;
    fusedwasi_p2_descriptor_get(s, self); /* only a hint: accepted, and nothing done */
    fusedwasi_p2_status(s, comp, retptr, FUSEDWASI_P2_OK);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_sync_data(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_status(s, comp, retptr, fusedwasi_p2_sys_fsync(fusedwasi_p2_descriptor_get(s, self), 1));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_sync(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_status(s, comp, retptr, fusedwasi_p2_sys_fsync(fusedwasi_p2_descriptor_get(s, self), 0));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_get_flags(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_descriptor *d = fusedwasi_p2_descriptor_get(s, self);
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_store_u8(s, comp, retptr + 1, (uint8_t)d->flags);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_get_type(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_descriptor *d = fusedwasi_p2_descriptor_get(s, self);
    fusedwasi_p2_stat st;
    int rc = fusedwasi_p2_sys_fstat(d, &st);
    if (rc != FUSEDWASI_P2_OK) {
        fusedwasi_p2_fs_fail(s, comp, retptr, retptr + 1, rc);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_store_u8(s, comp, retptr + 1, st.type);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_set_size(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t size, uint32_t retptr) {
    fusedwasi_p2_descriptor *d = fusedwasi_p2_descriptor_get(s, self);
    int rc = fusedwasi_p2_need(d, FUSEDWASI_P2_WRITE);
    if (rc == FUSEDWASI_P2_OK) rc = fusedwasi_p2_sys_ftruncate(d->fd, size);
    fusedwasi_p2_status(s, comp, retptr, rc);
}

/* new-timestamp: 0 no-change, 1 now, 2 timestamp */
FUSEDWASI_P2_FN int64_t fusedwasi_p2_new_time(uint32_t tag, uint64_t seconds, uint32_t nanos, int64_t current_ns, int64_t now_ns) {
    if (tag == 0) return current_ns;
    if (tag == 1) return now_ns;
    return (int64_t)seconds * 1000000000LL + nanos;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_set_times(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t a_tag, uint64_t a_seconds, uint32_t a_nanos,
                                                           uint32_t m_tag, uint64_t m_seconds, uint32_t m_nanos, uint32_t retptr) {
    fusedwasi_p2_descriptor *d = fusedwasi_p2_descriptor_get(s, self);
    int rc = fusedwasi_p2_need(d, d->is_dir ? FUSEDWASI_P2_MUTATE : FUSEDWASI_P2_WRITE);
    if (rc == FUSEDWASI_P2_OK) {
        fusedwasi_p2_stat st;
        rc = fusedwasi_p2_sys_fstat(d, &st);
        if (rc == FUSEDWASI_P2_OK) {
            int64_t now = (int64_t)s->wall_ns(s);
            rc = fusedwasi_p2_sys_set_times_fd(d, fusedwasi_p2_new_time(a_tag, a_seconds, a_nanos, st.atime_ns, now), fusedwasi_p2_new_time(m_tag, m_seconds, m_nanos, st.mtime_ns, now));
        }
    }
    fusedwasi_p2_status(s, comp, retptr, rc);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_read(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint64_t length, uint64_t offset, uint32_t retptr) {
    fusedwasi_p2_descriptor *d = fusedwasi_p2_descriptor_get(s, self);
    int rc = fusedwasi_p2_need(d, FUSEDWASI_P2_READ);
    uint64_t want = length < FUSEDWASI_P2_READ_CAP ? length : FUSEDWASI_P2_READ_CAP;
    uint8_t *buf = (uint8_t *)malloc(want ? (size_t)want : 1);
    if (!buf) s->trap(s, "out of memory");
    int64_t n = 0;
    if (rc == FUSEDWASI_P2_OK) n = fusedwasi_p2_sys_pread(d->fd, buf, (size_t)want, offset, &rc);
    if (rc != FUSEDWASI_P2_OK) {
        free(buf);
        fusedwasi_p2_fs_fail(s, comp, retptr, retptr + 4, rc);
        return;
    }
    uint32_t ptr = fusedwasi_p2_alloc(s, comp, 1, (uint32_t)n);
    if (n) memcpy(fusedwasi_p2_mem(s, comp, ptr, (uint32_t)n), buf, (size_t)n);
    free(buf);
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_store_u32(s, comp, retptr + 4, ptr);
    fusedwasi_p2_store_u32(s, comp, retptr + 8, (uint32_t)n);
    fusedwasi_p2_store_u8(s, comp, retptr + 12, (uint8_t)(n == 0)); /* end of file is when nothing was read (as in Wasmtime) */
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_write(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t length, uint64_t offset,
                                                       uint32_t retptr) {
    fusedwasi_p2_descriptor *d = fusedwasi_p2_descriptor_get(s, self);
    const uint8_t *data = fusedwasi_p2_mem(s, comp, ptr, length);
    int rc = fusedwasi_p2_need(d, FUSEDWASI_P2_WRITE);
    uint32_t done = 0;
    while (rc == FUSEDWASI_P2_OK && done < length) {
        int64_t n = fusedwasi_p2_sys_pwrite(d->fd, data + done, length - done, offset + done, &rc);
        if (n >= 0) done += (uint32_t)n;
    }
    if (rc != FUSEDWASI_P2_OK) {
        fusedwasi_p2_fs_fail(s, comp, retptr, retptr + 8, rc);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_store_u64(s, comp, retptr + 8, length);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_read_directory(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_descriptor *d = fusedwasi_p2_descriptor_get(s, self);
    if (!d->is_dir) {
        fusedwasi_p2_handle_result(s, comp, retptr, FUSEDWASI_P2_E_NOT_DIRECTORY, 0);
        return;
    }
    char **names;
    uint8_t *types;
    uint32_t count;
    int rc = fusedwasi_p2_sys_list(d, &names, &types, &count, s);
    if (rc != FUSEDWASI_P2_OK) {
        fusedwasi_p2_handle_result(s, comp, retptr, rc, 0);
        return;
    }
    fusedwasi_p2_dir_entries *ent = (fusedwasi_p2_dir_entries *)calloc(1, sizeof *ent);
    if (!ent) s->trap(s, "out of memory");
    ent->count = count, ent->names = names, ent->types = types;
    fusedwasi_p2_handle_result(s, comp, retptr, FUSEDWASI_P2_OK, fusedwasi_p2_table_new(s, FUSEDWASI_P2_KIND_DIRECTORY_ENTRY_STREAM, ent, fusedwasi_p2_dir_entries_destroy));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_directory_entry_stream_read_directory_entry(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_dir_entries *e = (fusedwasi_p2_dir_entries *)fusedwasi_p2_table_get(s, self, FUSEDWASI_P2_KIND_DIRECTORY_ENTRY_STREAM)->obj;
    fusedwasi_p2_ok(s, comp, retptr);
    if (e->pos >= e->count) {
        fusedwasi_p2_store_u8(s, comp, retptr + 4, 0);
        return;
    }
    uint32_t i = e->pos++;
    fusedwasi_p2_store_u8(s, comp, retptr + 4, 1);
    fusedwasi_p2_store_u8(s, comp, retptr + 8, e->types[i]);
    fusedwasi_p2_string(s, comp, e->names[i], retptr + 12);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_stat(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_descriptor *d = fusedwasi_p2_descriptor_get(s, self);
    fusedwasi_p2_stat st;
    int rc = fusedwasi_p2_sys_fstat(d, &st);
    if (rc != FUSEDWASI_P2_OK) {
        fusedwasi_p2_fs_fail(s, comp, retptr, retptr + 8, rc);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_write_stat(s, comp, retptr + 8, &st);
}

/* the directory descriptor `self` and the path, walked: FUSEDWASI_P2_OK or the code */
FUSEDWASI_P2_FN int fusedwasi_p2_begin(fusedwasi_p2_state *s, uint32_t comp, fusedwasi_p2_walk *w, uint32_t self, uint32_t ptr, uint32_t length, int follow, uint32_t need,
                             fusedwasi_p2_descriptor **out) {
    fusedwasi_p2_descriptor *d = fusedwasi_p2_descriptor_get(s, self);
    const uint8_t *path = fusedwasi_p2_mem(s, comp, ptr, length);
    *out = d;
    if (need) {
        int rc = fusedwasi_p2_need(d, need);
        if (rc != FUSEDWASI_P2_OK) return rc;
    }
    if (!d->is_dir) return FUSEDWASI_P2_E_NOT_DIRECTORY;
    return fusedwasi_p2_walk_path(w, d, path, length, follow);
}

FUSEDWASI_P2_FN int fusedwasi_p2_stat_at_rc(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t path_flags, uint32_t ptr, uint32_t length, fusedwasi_p2_stat *st) {
    fusedwasi_p2_walk w;
    fusedwasi_p2_descriptor *d;
    int rc = fusedwasi_p2_begin(s, comp, &w, self, ptr, length, path_flags & FUSEDWASI_P2_SYMLINK_FOLLOW, 0, &d);
    if (rc != FUSEDWASI_P2_OK) return rc;
    rc = fusedwasi_p2_sys_lstat(w.dir, w.leaf, st);
    fusedwasi_p2_walk_close(&w);
    return rc;
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_stat_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t path_flags, uint32_t ptr, uint32_t length,
                                                         uint32_t retptr) {
    fusedwasi_p2_stat st;
    int rc = fusedwasi_p2_stat_at_rc(s, comp, self, path_flags, ptr, length, &st);
    if (rc != FUSEDWASI_P2_OK) {
        fusedwasi_p2_fs_fail(s, comp, retptr, retptr + 8, rc);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_write_stat(s, comp, retptr + 8, &st);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_set_times_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t path_flags, uint32_t ptr, uint32_t length,
                                                              uint32_t a_tag, uint64_t a_seconds, uint32_t a_nanos, uint32_t m_tag, uint64_t m_seconds,
                                                              uint32_t m_nanos, uint32_t retptr) {
    fusedwasi_p2_walk w;
    fusedwasi_p2_descriptor *d;
    int rc = fusedwasi_p2_begin(s, comp, &w, self, ptr, length, path_flags & FUSEDWASI_P2_SYMLINK_FOLLOW, FUSEDWASI_P2_MUTATE, &d);
    if (rc == FUSEDWASI_P2_OK) {
        fusedwasi_p2_stat st;
        rc = fusedwasi_p2_sys_lstat(w.dir, w.leaf, &st);
        if (rc == FUSEDWASI_P2_OK) {
            int64_t now = (int64_t)s->wall_ns(s);
            rc = fusedwasi_p2_sys_set_times(w.dir, w.leaf, fusedwasi_p2_new_time(a_tag, a_seconds, a_nanos, st.atime_ns, now),
                                       fusedwasi_p2_new_time(m_tag, m_seconds, m_nanos, st.mtime_ns, now));
        }
        fusedwasi_p2_walk_close(&w);
    }
    fusedwasi_p2_status(s, comp, retptr, rc);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_link_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t old_flags, uint32_t old_ptr, uint32_t old_len,
                                                         uint32_t new_dir, uint32_t new_ptr, uint32_t new_len, uint32_t retptr) {
    fusedwasi_p2_walk w1, w2;
    fusedwasi_p2_descriptor *d, *n = fusedwasi_p2_descriptor_get(s, new_dir);
    int rc = fusedwasi_p2_begin(s, comp, &w1, self, old_ptr, old_len, old_flags & FUSEDWASI_P2_SYMLINK_FOLLOW, FUSEDWASI_P2_MUTATE, &d);
    if (rc == FUSEDWASI_P2_OK) {
        rc = fusedwasi_p2_need(n, FUSEDWASI_P2_MUTATE);
        if (rc == FUSEDWASI_P2_OK) rc = fusedwasi_p2_begin(s, comp, &w2, new_dir, new_ptr, new_len, 0, FUSEDWASI_P2_MUTATE, &n);
        if (rc == FUSEDWASI_P2_OK) {
            rc = fusedwasi_p2_sys_link(w1.dir, w1.leaf, w2.dir, w2.leaf);
            fusedwasi_p2_walk_close(&w2);
        }
        fusedwasi_p2_walk_close(&w1);
    }
    fusedwasi_p2_status(s, comp, retptr, rc);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_open_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t path_flags, uint32_t ptr, uint32_t length,
                                                         uint32_t open_flags, uint32_t flags, uint32_t retptr) {
    fusedwasi_p2_descriptor *d = fusedwasi_p2_descriptor_get(s, self);
    const uint8_t *path = fusedwasi_p2_mem(s, comp, ptr, length);
    int rc = FUSEDWASI_P2_OK;
    uint32_t handle = 0;
    if (!d->is_dir) {
        rc = FUSEDWASI_P2_E_NOT_DIRECTORY;
    } else if ((flags & (FUSEDWASI_P2_WRITE | FUSEDWASI_P2_MUTATE)) && !(d->flags & FUSEDWASI_P2_MUTATE)) {
        rc = FUSEDWASI_P2_E_READ_ONLY; /* a descriptor cannot hand out more than it has (as the WIT says for open-at) */
    } else if ((flags & FUSEDWASI_P2_READ) && !(d->flags & FUSEDWASI_P2_READ)) {
        rc = FUSEDWASI_P2_E_NOT_PERMITTED;
    } else if ((open_flags & (FUSEDWASI_P2_CREATE | FUSEDWASI_P2_TRUNCATE)) && !(d->flags & FUSEDWASI_P2_MUTATE)) {
        rc = FUSEDWASI_P2_E_READ_ONLY;
    } else {
        fusedwasi_p2_walk w;
        rc = fusedwasi_p2_walk_path(&w, d, path, length, path_flags & FUSEDWASI_P2_SYMLINK_FOLLOW);
        if (rc == FUSEDWASI_P2_OK) {
            fusedwasi_p2_descriptor made;
            rc = fusedwasi_p2_sys_open_at(w.dir, w.leaf, open_flags, flags, &made);
            /* what a directory may do is what the directory it was opened under may do (as in Wasmtime, where a child directory gets the permissions of the preopen), and not
               what was asked for: a program of WASI 0.1 opens a directory with the rights it needs to read it and then creates files in it */
            if (rc == FUSEDWASI_P2_OK) handle = fusedwasi_p2_descriptor_new(s, &made, made.is_dir ? (d->flags & (FUSEDWASI_P2_READ | FUSEDWASI_P2_MUTATE)) : flags);
            fusedwasi_p2_walk_close(&w);
        }
    }
    fusedwasi_p2_handle_result(s, comp, retptr, rc, handle);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_readlink_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t length, uint32_t retptr) {
    fusedwasi_p2_walk w;
    fusedwasi_p2_descriptor *d;
    char target[FUSEDWASI_P2_PATH_MAX];
    int rc = fusedwasi_p2_begin(s, comp, &w, self, ptr, length, 0, 0, &d);
    if (rc == FUSEDWASI_P2_OK) {
        rc = fusedwasi_p2_sys_readlink(w.dir, w.leaf, target, sizeof target);
        fusedwasi_p2_walk_close(&w);
    }
    if (rc != FUSEDWASI_P2_OK) {
        fusedwasi_p2_fs_fail(s, comp, retptr, retptr + 4, rc);
        return;
    }
    if (target[0] == '/') { /* an absolute or rooted target is not shown */
        fusedwasi_p2_fs_fail(s, comp, retptr, retptr + 4, FUSEDWASI_P2_E_NOT_PERMITTED);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
    fusedwasi_p2_string(s, comp, target, retptr + 4);
}

typedef int (*fusedwasi_p2_name_op)(fusedwasi_p2_dirref dir, const char *leaf, const void *arg);

FUSEDWASI_P2_FN void fusedwasi_p2_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr, uint32_t self, uint32_t ptr, uint32_t length, fusedwasi_p2_name_op op, const void *arg) {
    fusedwasi_p2_walk w;
    fusedwasi_p2_descriptor *d;
    int rc = fusedwasi_p2_begin(s, comp, &w, self, ptr, length, 0, FUSEDWASI_P2_MUTATE, &d);
    if (rc == FUSEDWASI_P2_OK) {
        rc = op(w.dir, w.leaf, arg);
        fusedwasi_p2_walk_close(&w);
    }
    fusedwasi_p2_status(s, comp, retptr, rc);
}

FUSEDWASI_P2_FN int fusedwasi_p2_op_mkdir(fusedwasi_p2_dirref dir, const char *leaf, const void *arg) {
    (void)arg;
    return fusedwasi_p2_sys_mkdir(dir, leaf);
}
FUSEDWASI_P2_FN int fusedwasi_p2_op_rmdir(fusedwasi_p2_dirref dir, const char *leaf, const void *arg) {
    (void)arg;
    return fusedwasi_p2_sys_rmdir(dir, leaf);
}
FUSEDWASI_P2_FN int fusedwasi_p2_op_unlink(fusedwasi_p2_dirref dir, const char *leaf, const void *arg) {
    (void)arg;
    return fusedwasi_p2_sys_unlink(dir, leaf);
}
FUSEDWASI_P2_FN int fusedwasi_p2_op_symlink(fusedwasi_p2_dirref dir, const char *leaf, const void *arg) { return fusedwasi_p2_sys_symlink((const char *)arg, dir, leaf); }

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_create_directory_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t length,
                                                                     uint32_t retptr) {
    fusedwasi_p2_at(s, comp, retptr, self, ptr, length, fusedwasi_p2_op_mkdir, NULL);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_remove_directory_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t length,
                                                                     uint32_t retptr) {
    fusedwasi_p2_at(s, comp, retptr, self, ptr, length, fusedwasi_p2_op_rmdir, NULL);
}
FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_unlink_file_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t ptr, uint32_t length, uint32_t retptr) {
    fusedwasi_p2_at(s, comp, retptr, self, ptr, length, fusedwasi_p2_op_unlink, NULL);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_rename_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t old_ptr, uint32_t old_len, uint32_t new_dir,
                                                           uint32_t new_ptr, uint32_t new_len, uint32_t retptr) {
    fusedwasi_p2_walk w1, w2;
    fusedwasi_p2_descriptor *d, *n = fusedwasi_p2_descriptor_get(s, new_dir);
    int rc = fusedwasi_p2_begin(s, comp, &w1, self, old_ptr, old_len, 0, FUSEDWASI_P2_MUTATE, &d);
    if (rc == FUSEDWASI_P2_OK) {
        rc = fusedwasi_p2_need(n, FUSEDWASI_P2_MUTATE);
        if (rc == FUSEDWASI_P2_OK) rc = fusedwasi_p2_begin(s, comp, &w2, new_dir, new_ptr, new_len, 0, FUSEDWASI_P2_MUTATE, &n);
        if (rc == FUSEDWASI_P2_OK) {
            rc = fusedwasi_p2_sys_rename(w1.dir, w1.leaf, w2.dir, w2.leaf);
            fusedwasi_p2_walk_close(&w2);
        }
        fusedwasi_p2_walk_close(&w1);
    }
    fusedwasi_p2_status(s, comp, retptr, rc);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_symlink_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t old_ptr, uint32_t old_len, uint32_t new_ptr,
                                                            uint32_t new_len, uint32_t retptr) {
    const uint8_t *target = fusedwasi_p2_mem(s, comp, old_ptr, old_len);
    if (memchr(target, 0, old_len) || (old_len && target[0] == '/')) {
        fusedwasi_p2_descriptor_get(s, self);
        fusedwasi_p2_status(s, comp, retptr, memchr(target, 0, old_len) ? FUSEDWASI_P2_E_INVALID : FUSEDWASI_P2_E_NOT_PERMITTED);
        return;
    }
    char *copy = (char *)malloc((size_t)old_len + 1);
    if (!copy) s->trap(s, "out of memory");
    memcpy(copy, target, old_len);
    copy[old_len] = '\0';
    fusedwasi_p2_at(s, comp, retptr, self, new_ptr, new_len, fusedwasi_p2_op_symlink, copy);
    free(copy);
}

FUSEDWASI_P2_FN uint32_t fusedwasi_wasi_filesystem_types_descriptor_is_same_object(fusedwasi_p2_state *s, uint32_t self, uint32_t other) {
    fusedwasi_p2_stat a, b;
    int r1 = fusedwasi_p2_sys_fstat(fusedwasi_p2_descriptor_get(s, self), &a), r2 = fusedwasi_p2_sys_fstat(fusedwasi_p2_descriptor_get(s, other), &b);
    return (uint32_t)(r1 == FUSEDWASI_P2_OK && r2 == FUSEDWASI_P2_OK && a.dev == b.dev && a.ino == b.ino);
}

FUSEDWASI_P2_FN uint64_t fusedwasi_p2_fnv(const uint64_t *values, int n) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (int i = 0; i < n; i++)
        for (int b = 0; b < 8; b++) h = (h ^ ((values[i] >> (8 * b)) & 0xFF)) * 0x100000001B3ULL;
    return h;
}

FUSEDWASI_P2_FN void fusedwasi_p2_hash_result(fusedwasi_p2_state *s, uint32_t comp, uint32_t retptr, int rc, const fusedwasi_p2_stat *st) {
    if (rc != FUSEDWASI_P2_OK) {
        fusedwasi_p2_fs_fail(s, comp, retptr, retptr + 8, rc);
        return;
    }
    fusedwasi_p2_ok(s, comp, retptr);
    /* `upper` names the object (device, inode); `lower` also takes in what changes when it is modified (size, modification time): FNV-1a 64 as the Python twin */
    uint64_t lower[4] = {st->size, (uint64_t)st->mtime_ns, st->dev, st->ino};
    uint64_t upper[2] = {st->dev, st->ino};
    fusedwasi_p2_store_u64(s, comp, retptr + 8, fusedwasi_p2_fnv(lower, 4));
    fusedwasi_p2_store_u64(s, comp, retptr + 16, fusedwasi_p2_fnv(upper, 2));
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_metadata_hash(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t retptr) {
    fusedwasi_p2_stat st;
    fusedwasi_p2_hash_result(s, comp, retptr, fusedwasi_p2_sys_fstat(fusedwasi_p2_descriptor_get(s, self), &st), &st);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_descriptor_metadata_hash_at(fusedwasi_p2_state *s, uint32_t comp, uint32_t self, uint32_t path_flags, uint32_t ptr,
                                                                  uint32_t length, uint32_t retptr) {
    fusedwasi_p2_stat st;
    fusedwasi_p2_hash_result(s, comp, retptr, fusedwasi_p2_stat_at_rc(s, comp, self, path_flags, ptr, length, &st), &st);
}

FUSEDWASI_P2_FN void fusedwasi_wasi_filesystem_types_filesystem_error_code(fusedwasi_p2_state *s, uint32_t comp, uint32_t err, uint32_t retptr) {
    fusedwasi_p2_error *e = (fusedwasi_p2_error *)fusedwasi_p2_table_get(s, err, FUSEDWASI_P2_KIND_ERROR)->obj;
    if (e->code < 0) {
        fusedwasi_p2_store_u8(s, comp, retptr, 0);
        return;
    }
    fusedwasi_p2_store_u8(s, comp, retptr, 1);
    fusedwasi_p2_store_u8(s, comp, retptr + 1, (uint8_t)e->code);
}

#endif /* FUSEDWASI_P2_FILESYSTEM_H */
