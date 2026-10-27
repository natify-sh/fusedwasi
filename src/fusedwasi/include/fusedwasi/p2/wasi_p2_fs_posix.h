/* The system calls of wasi_p2_filesystem.h on POSIX: descriptors relative to a directory (openat and the other *at calls). Included by that file; the Windows twin is
 * wasi_p2_fs_win32.h. Every function returns FUSEDWASI_P2_OK or an error-code (the confinement of paths is in `fusedwasi_p2_walk_path`, described in the Python twin). */
#ifndef FUSEDWASI_P2_FS_POSIX_H
#define FUSEDWASI_P2_FS_POSIX_H

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef int fusedwasi_p2_dirref; /* the descriptor of the directory */
#define FUSEDWASI_P2_DIRREF_OF(d) ((d)->fd)

#ifdef __APPLE__
#define FUSEDWASI_P2_ATIME(st) ((st).st_atimespec)
#define FUSEDWASI_P2_MTIME(st) ((st).st_mtimespec)
#define FUSEDWASI_P2_CTIME(st) ((st).st_ctimespec)
#else
#define FUSEDWASI_P2_ATIME(st) ((st).st_atim)
#define FUSEDWASI_P2_MTIME(st) ((st).st_mtim)
#define FUSEDWASI_P2_CTIME(st) ((st).st_ctim)
#endif

FUSEDWASI_P2_FN int64_t fusedwasi_p2_ts_ns(struct timespec t) { return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec; }

FUSEDWASI_P2_FN uint8_t fusedwasi_p2_type_of(mode_t m) {
    if (S_ISREG(m)) return FUSEDWASI_P2_T_REGULAR;
    if (S_ISDIR(m)) return FUSEDWASI_P2_T_DIRECTORY;
    if (S_ISLNK(m)) return FUSEDWASI_P2_T_SYMLINK;
    if (S_ISCHR(m)) return FUSEDWASI_P2_T_CHAR;
    if (S_ISBLK(m)) return FUSEDWASI_P2_T_BLOCK;
    if (S_ISFIFO(m)) return FUSEDWASI_P2_T_FIFO;
    if (S_ISSOCK(m)) return FUSEDWASI_P2_T_SOCKET;
    return FUSEDWASI_P2_T_UNKNOWN;
}

FUSEDWASI_P2_FN void fusedwasi_p2_stat_from(const struct stat *st, fusedwasi_p2_stat *out) {
    out->type = fusedwasi_p2_type_of(st->st_mode);
    out->nlink = (uint64_t)st->st_nlink;
    out->size = (uint64_t)st->st_size;
    out->dev = (uint64_t)st->st_dev;
    out->ino = (uint64_t)st->st_ino;
    out->atime_ns = fusedwasi_p2_ts_ns(FUSEDWASI_P2_ATIME(*st));
    out->mtime_ns = fusedwasi_p2_ts_ns(FUSEDWASI_P2_MTIME(*st));
    out->ctime_ns = fusedwasi_p2_ts_ns(FUSEDWASI_P2_CTIME(*st));
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_lstat(fusedwasi_p2_dirref dir, const char *leaf, fusedwasi_p2_stat *out) {
    struct stat st;
    if (fstatat(dir, leaf, &st, AT_SYMLINK_NOFOLLOW) < 0) return fusedwasi_p2_fail();
    fusedwasi_p2_stat_from(&st, out);
    return FUSEDWASI_P2_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_fstat(const fusedwasi_p2_descriptor *d, fusedwasi_p2_stat *out) {
    struct stat st;
    if (fstat(d->fd, &st) < 0) return fusedwasi_p2_fail();
    fusedwasi_p2_stat_from(&st, out);
    return FUSEDWASI_P2_OK;
}

/* the target of a link, NUL-terminated */
FUSEDWASI_P2_FN int fusedwasi_p2_sys_readlink(fusedwasi_p2_dirref dir, const char *leaf, char *out, size_t cap) {
    fusedwasi_p2_ssize n = readlinkat(dir, leaf, out, cap - 1);
    if (n < 0) return fusedwasi_p2_fail();
    out[n] = '\0';
    return FUSEDWASI_P2_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_mkdir(fusedwasi_p2_dirref dir, const char *leaf) { return mkdirat(dir, leaf, 0777) < 0 ? fusedwasi_p2_fail() : FUSEDWASI_P2_OK; }
FUSEDWASI_P2_FN int fusedwasi_p2_sys_rmdir(fusedwasi_p2_dirref dir, const char *leaf) { return unlinkat(dir, leaf, AT_REMOVEDIR) < 0 ? fusedwasi_p2_fail() : FUSEDWASI_P2_OK; }

FUSEDWASI_P2_FN int fusedwasi_p2_sys_unlink(fusedwasi_p2_dirref dir, const char *leaf) {
    if (unlinkat(dir, leaf, 0) == 0) return FUSEDWASI_P2_OK;
    int e = errno;
    struct stat st;
    if (e == EPERM && fstatat(dir, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(st.st_mode)) return FUSEDWASI_P2_E_IS_DIRECTORY; /* some systems say EPERM */
    return fusedwasi_p2_errno_code(e);
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_rename(fusedwasi_p2_dirref d1, const char *l1, fusedwasi_p2_dirref d2, const char *l2) {
    return renameat(d1, l1, d2, l2) < 0 ? fusedwasi_p2_fail() : FUSEDWASI_P2_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_link(fusedwasi_p2_dirref d1, const char *l1, fusedwasi_p2_dirref d2, const char *l2) {
    return linkat(d1, l1, d2, l2, 0) < 0 ? fusedwasi_p2_fail() : FUSEDWASI_P2_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_symlink(const char *target, fusedwasi_p2_dirref dir, const char *leaf) { return symlinkat(target, dir, leaf) < 0 ? fusedwasi_p2_fail() : FUSEDWASI_P2_OK; }

FUSEDWASI_P2_FN struct timespec fusedwasi_p2_ns_ts(int64_t ns) {
    struct timespec t;
    t.tv_sec = (time_t)(ns / 1000000000LL);
    t.tv_nsec = (long)(ns % 1000000000LL);
    return t;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_set_times(fusedwasi_p2_dirref dir, const char *leaf, int64_t atime_ns, int64_t mtime_ns) {
    struct timespec ts[2] = {fusedwasi_p2_ns_ts(atime_ns), fusedwasi_p2_ns_ts(mtime_ns)};
    return utimensat(dir, leaf, ts, AT_SYMLINK_NOFOLLOW) < 0 ? fusedwasi_p2_fail() : FUSEDWASI_P2_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_set_times_fd(const fusedwasi_p2_descriptor *d, int64_t atime_ns, int64_t mtime_ns) {
    struct timespec ts[2] = {fusedwasi_p2_ns_ts(atime_ns), fusedwasi_p2_ns_ts(mtime_ns)};
    return futimens(d->fd, ts) < 0 ? fusedwasi_p2_fail() : FUSEDWASI_P2_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_open_at(fusedwasi_p2_dirref dir, const char *leaf, uint32_t open_flags, uint32_t flags, fusedwasi_p2_descriptor *out) {
    int oflags = O_NOFOLLOW | O_CLOEXEC;
    oflags |= (flags & FUSEDWASI_P2_READ) && (flags & FUSEDWASI_P2_WRITE) ? O_RDWR : (flags & FUSEDWASI_P2_WRITE) ? O_WRONLY : O_RDONLY;
    if (open_flags & FUSEDWASI_P2_CREATE) oflags |= O_CREAT;
    if (open_flags & FUSEDWASI_P2_DIRECTORY) oflags |= O_DIRECTORY;
    if (open_flags & FUSEDWASI_P2_EXCLUSIVE) oflags |= O_EXCL;
    if (open_flags & FUSEDWASI_P2_TRUNCATE) oflags |= O_TRUNC;
    if (flags & FUSEDWASI_P2_FILE_SYNC) oflags |= O_SYNC;
    if (flags & FUSEDWASI_P2_DATA_SYNC) oflags |= O_DSYNC;
    int fd = openat(dir, leaf, oflags, 0666);
    if (fd < 0) {
        int rc = fusedwasi_p2_fail();
        char probe[1];
        size_t leaf_len = strlen(leaf);
        /* a link is not entered when links are not followed (O_NOFOLLOW) */
        if (errno == ENOTDIR && (open_flags & FUSEDWASI_P2_DIRECTORY) && leaf_len && leaf[leaf_len - 1] != '/' && readlinkat(dir, leaf, probe, sizeof probe) >= 0)
            rc = FUSEDWASI_P2_E_LOOP;
        return rc;
    }
    struct stat st;
    out->fd = fd;
    out->is_dir = fstat(fd, &st) == 0 && S_ISDIR(st.st_mode);
    out->path = NULL;
    return FUSEDWASI_P2_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_open_directory(const char *path, fusedwasi_p2_descriptor *out) {
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return fusedwasi_p2_fail();
    out->fd = fd, out->is_dir = 1, out->path = NULL;
    return FUSEDWASI_P2_OK;
}

FUSEDWASI_P2_FN void fusedwasi_p2_sys_close(fusedwasi_p2_descriptor *d) {
    if (d->fd >= 0) close(d->fd);
    d->fd = -1;
    free(d->path);
    d->path = NULL;
}

/* file contents by offset, on a descriptor: -1 and the code in *rc for a failure */
FUSEDWASI_P2_FN int64_t fusedwasi_p2_sys_pread(int fd, void *buf, size_t n, uint64_t off, int *rc) {
    fusedwasi_p2_ssize r = pread(fd, buf, n, (off_t)off);
    if (r < 0) *rc = fusedwasi_p2_fail();
    return r;
}

FUSEDWASI_P2_FN int64_t fusedwasi_p2_sys_pwrite(int fd, const void *buf, size_t n, uint64_t off, int *rc) {
    for (;;) {
        fusedwasi_p2_ssize r = pwrite(fd, buf, n, (off_t)off);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) *rc = fusedwasi_p2_fail();
        return r;
    }
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_size(int fd, uint64_t *size) {
    struct stat sb;
    if (fstat(fd, &sb) < 0) return fusedwasi_p2_fail();
    *size = (uint64_t)sb.st_size;
    return FUSEDWASI_P2_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_sys_dup(int fd) { return dup(fd); }
FUSEDWASI_P2_FN void fusedwasi_p2_sys_closefd(int fd) {
    if (fd >= 0) close(fd);
}
FUSEDWASI_P2_FN int fusedwasi_p2_sys_ftruncate(int fd, uint64_t size) { return ftruncate(fd, (off_t)size) < 0 ? fusedwasi_p2_fail() : FUSEDWASI_P2_OK; }

FUSEDWASI_P2_FN int fusedwasi_p2_sys_fsync(const fusedwasi_p2_descriptor *d, int data_only) {
#ifdef __APPLE__
    (void)data_only;
    int r = fsync(d->fd);
#else
    int r = data_only ? fdatasync(d->fd) : fsync(d->fd);
#endif
    return r < 0 ? fusedwasi_p2_fail() : FUSEDWASI_P2_OK;
}

FUSEDWASI_P2_FN int fusedwasi_p2_name_cmp(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b); /* the names are sorted with their types: see below */
}

FUSEDWASI_P2_FN void fusedwasi_p2_names_free(char **names, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) free(names[i]);
    free(names);
}

/* the names of a directory, sorted, without . and .., with their types; the caller frees them. No memory for the list is FUSEDWASI_P2_E_INSUFFICIENT_MEMORY (nothing is kept). */
FUSEDWASI_P2_FN int fusedwasi_p2_sys_list(const fusedwasi_p2_descriptor *d, char ***names_out, uint8_t **types_out, uint32_t *count_out) {
    int dupfd = dup(d->fd);
    DIR *dir = dupfd < 0 ? NULL : fdopendir(dupfd);
    if (!dir) {
        int rc = fusedwasi_p2_fail();
        if (dupfd >= 0) close(dupfd);
        return rc;
    }
    rewinddir(dir);
    uint32_t count = 0, capacity = 16;
    char **names = (char **)malloc(capacity * sizeof *names);
    if (!names) {
        closedir(dir);
        return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (count == capacity) {
            capacity *= 2;
            char **bigger = (char **)realloc(names, capacity * sizeof *names);
            if (!bigger) {
                fusedwasi_p2_names_free(names, count);
                closedir(dir);
                return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
            }
            names = bigger;
        }
        names[count] = strdup(e->d_name);
        if (!names[count]) {
            fusedwasi_p2_names_free(names, count);
            closedir(dir);
            return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
        }
        count++;
    }
    closedir(dir);
    qsort(names, count, sizeof *names, fusedwasi_p2_name_cmp);
    uint8_t *types = (uint8_t *)malloc(count ? count : 1);
    if (!types) {
        fusedwasi_p2_names_free(names, count);
        return FUSEDWASI_P2_E_INSUFFICIENT_MEMORY;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        fusedwasi_p2_stat st;
        if (fusedwasi_p2_sys_lstat(d->fd, names[i], &st) != FUSEDWASI_P2_OK) { /* gone since the listing */
            free(names[i]);
            continue;
        }
        names[kept] = names[i];
        types[kept++] = st.type;
    }
    *names_out = names, *types_out = types, *count_out = kept;
    return FUSEDWASI_P2_OK;
}

/* the walk of a path, confined below a directory (see the Python twin): `dir` and `leaf` are the directory that holds the last name and that name ("." for none);
   `fusedwasi_p2_walk_close` closes what the walk opened. */
typedef struct {
    int dir;
    char leaf[FUSEDWASI_P2_NAME_MAX + 2];
    int fds[FUSEDWASI_P2_MAX_DEPTH + 2]; /* [0] is the base, the others are opened by the walk */
    int depth;
} fusedwasi_p2_walk;

FUSEDWASI_P2_FN void fusedwasi_p2_walk_close(fusedwasi_p2_walk *w) {
    for (int i = 1; i <= w->depth; i++) close(w->fds[i]);
    w->depth = 0;
}

/* `link_slash_not_dir`: a link with a slash after it, where links are not followed, is not a directory (WASI 0.1: uvwasi says so); else the walk follows it, below the base (Wasmtime) */
FUSEDWASI_P2_FN int fusedwasi_p2_walk_path_with(fusedwasi_p2_walk *w, const fusedwasi_p2_descriptor *base_d, const uint8_t *path, uint32_t len, int follow, int link_slash_not_dir) {
    int base = base_d->fd;
    char work[FUSEDWASI_P2_PATH_MAX], name[FUSEDWASI_P2_NAME_MAX + 2], target[FUSEDWASI_P2_PATH_MAX];
    int links = 0, rc = FUSEDWASI_P2_OK;
    w->depth = 0;
    w->fds[0] = base;
    if (memchr(path, 0, len)) return FUSEDWASI_P2_E_INVALID;
    if (len == 0) return FUSEDWASI_P2_E_NO_ENTRY; /* an empty name is no name (POSIX; Wasmtime says the same) */
    if (len && path[0] == '/') return FUSEDWASI_P2_E_NOT_PERMITTED;
    if (len >= sizeof work) return FUSEDWASI_P2_E_NAME_TOO_LONG;
    memcpy(work, path, len);
    work[len] = '\0';
    char *cur = work;
    for (;;) {
        while (*cur == '/') cur++;
        if (*cur == '\0') {
            strcpy(w->leaf, ".");
            break;
        }
        char *end = strchr(cur, '/');
        size_t clen = end ? (size_t)(end - cur) : strlen(cur);
        char *rest = cur + clen;
        while (*rest == '/') rest++;
        int last = (*rest == '\0');
        int slash = last && end != NULL;
        if (clen > FUSEDWASI_P2_NAME_MAX) {
            rc = FUSEDWASI_P2_E_NAME_TOO_LONG;
            goto done;
        }
        memcpy(name, cur, clen);
        name[clen] = '\0';
        if (clen == 1 && name[0] == '.') {
            if (last) {
                strcpy(w->leaf, ".");
                break;
            }
            cur = rest;
            continue;
        }
        if (clen == 2 && name[0] == '.' && name[1] == '.') {
            if (w->depth == 0) {
                rc = FUSEDWASI_P2_E_NOT_PERMITTED;
                goto done;
            }
            close(w->fds[w->depth--]);
            if (last) {
                strcpy(w->leaf, ".");
                break;
            }
            cur = rest;
            continue;
        }
        if (last) {
            if (!follow) {
                /* a slash after a name makes the kernel follow a link there whatever O_NOFOLLOW says; the walk follows it itself, below the base */
                char probe[1];
                int is_link = slash && readlinkat(w->fds[w->depth], name, probe, sizeof probe) >= 0;
                if (is_link && link_slash_not_dir) {
                    rc = FUSEDWASI_P2_E_NOT_DIRECTORY;
                    goto done;
                }
                if (!is_link) {
                    strcpy(w->leaf, name);
                    if (slash) strcat(w->leaf, "/");
                    break;
                }
            }
        } else {
            if (w->depth > FUSEDWASI_P2_MAX_DEPTH) {
                rc = FUSEDWASI_P2_E_NAME_TOO_LONG;
                goto done;
            }
            int next = openat(w->fds[w->depth], name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next >= 0) {
                w->fds[++w->depth] = next;
                cur = rest;
                continue;
            }
            if (errno != ELOOP && errno != ENOTDIR && errno != EMLINK) {
                rc = fusedwasi_p2_fail();
                goto done;
            }
        }
        fusedwasi_p2_ssize n = readlinkat(w->fds[w->depth], name, target, sizeof target - 1);
        if (n < 0) {
            if (last) { /* not a link: this is the last name */
                strcpy(w->leaf, name);
                if (slash) strcat(w->leaf, "/");
                break;
            }
            rc = FUSEDWASI_P2_E_NOT_DIRECTORY;
            goto done;
        }
        target[n] = '\0';
        if (++links > FUSEDWASI_P2_MAX_LINKS) {
            rc = FUSEDWASI_P2_E_LOOP;
            goto done;
        }
        if (target[0] == '/') {
            rc = FUSEDWASI_P2_E_NOT_PERMITTED;
            goto done;
        }
        size_t rest_len = strlen(rest);
        if ((size_t)n + 1 + rest_len >= sizeof work) {
            rc = FUSEDWASI_P2_E_NAME_TOO_LONG;
            goto done;
        }
        memmove(work + n + 1, rest, rest_len + 1); /* rest may lie inside work */
        memcpy(work, target, (size_t)n);
        work[n] = '/';
        if (rest_len == 0 && !slash) work[n] = '\0';
        cur = work;
    }
    w->dir = w->fds[w->depth];
    return FUSEDWASI_P2_OK;
done:
    fusedwasi_p2_walk_close(w);
    return rc;
}

FUSEDWASI_P2_FN int fusedwasi_p2_walk_path(fusedwasi_p2_walk *w, const fusedwasi_p2_descriptor *base_d, const uint8_t *path, uint32_t len, int follow) {
    return fusedwasi_p2_walk_path_with(w, base_d, path, len, follow, 0);
}

#endif /* FUSEDWASI_P2_FS_POSIX_H */
